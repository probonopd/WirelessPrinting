// Required: https://github.com/greiman/SdFat

#include <ArduinoOTA.h>
#if defined(ESP8266)
  #include <ESP8266mDNS.h>        // https://github.com/esp8266/Arduino/tree/master/libraries/ESP8266mDNS
#elif defined(ESP32)
  #include <ESPmDNS.h>
#endif
#include <ArduinoJson.h>          // https://github.com/bblanchon/ArduinoJson (for implementing a subset of the OctoPrint API)
#include <DNSServer.h>
#include "StorageFS.h"
#include <ESPAsyncWebServer.h>    // https://github.com/me-no-dev/ESPAsyncWebServer
#include <ESPAsyncWiFiManager.h>  // https://github.com/alanswx/ESPAsyncWiFiManager/
#include <SPIFFSEditor.h>

#include "CommandQueue.h"

WiFiServer telnetServer(23);
WiFiClient serverClient;

AsyncWebServer server(80);
DNSServer dns;

// Configurable parameters
#define SKETCH_VERSION "2.x-localbuild" // Gets inserted at build time by .travis.yml
#define USE_FAST_SD                     // Use Default fast SD clock, comment if your SD is an old or slow one.
#define OTA_UPDATES                     // Enable OTA firmware updates, comment if you don't want it (OTA may lead to security issues because someone may load any code on device)
//#define OTA_PASSWORD ""               // Uncomment to protect OTA updates and assign a password (inside "")
#define MAX_SUPPORTED_EXTRUDERS 6       // Number of supported extruder

#define PRINTER_RX_BUFFER_SIZE 0        // This is printer firmware 'RX_BUFFER_SIZE'. If such parameter is unknown please use 0
#define TEMPERATURE_REPORT_INTERVAL 2   // Ask the printer for its temperatures status every 2 seconds
#define KEEPALIVE_INTERVAL 2500         // Marlin defaults to 2 seconds, get a little of margin
const uint32_t serialBauds[] = { 1000000, 500000, 250000, 115200, 57600 };   // Marlin valid bauds (removed very low bauds)

#define API_VERSION     "0.1"
#define VERSION         "1.3.10"

// The sketch on the ESP
bool ESPrestartRequired;  // Set this flag in the callbacks to restart ESP

// Information from M115
String fwMachineType = "Unknown";
uint8_t fwExtruders = 1;
bool fwAutoreportTempCap, fwProgressCap, fwBuildPercentCap;

// Printer status
bool printerConnected,
     startPrint,
     isPrinting,
     printPause,
     restartPrint,
     cancelPrint,
     autoreportTempEnabled;

uint32_t printStartTime;
float printCompletion;

// Serial communication
String lastCommandSent, lastReceivedResponse;
uint32_t lastPrintedLine;

uint8_t serialBaudIndex;
uint16_t printerUsedBuffer;
uint32_t serialReceiveTimeoutTimer;

// Uploaded file information
String uploadedFullname;
size_t uploadedFileSize, filePos;
uint32_t uploadedFileDate = 1378847754;

// Temperature for printer status reporting
#define TEMP_COMMAND      "M105"
#define AUTOTEMP_COMMAND  "M155 S"

struct Temperature {
  String actual, target;
};

uint32_t temperatureTimer;

Temperature toolTemperature[MAX_SUPPORTED_EXTRUDERS];
Temperature bedTemperature;


// https://forum.arduino.cc/index.php?topic=228884.msg2670971#msg2670971
inline String IpAddress2String(const IPAddress& ipAddress) {
  return String(ipAddress[0]) + "." +
         String(ipAddress[1]) + "." +
         String(ipAddress[2]) + "." +
         String(ipAddress[3]);
}

inline void setLed(const bool status) {
  digitalWrite(LED_BUILTIN, status ? LOW : HIGH);   // Note: LOW turn the LED on
}

inline void telnetSend(const String line) {
  if (serverClient && serverClient.connected())     // send data to telnet client if connected
    serverClient.println(line);
}

bool isFloat(const String value) {
  for (int i = 0; i < value.length(); ++i) {
    char ch = value[i];
    if (ch != ' ' && ch != '.' && ch != '-' && !isDigit(ch))
      return false;
  }

  return true;
}

// Parse temperatures from printer responses like
// ok T:32.8 /0.0 B:31.8 /0.0 T0:32.8 /0.0 @:0 B@:0
bool parseTemp(const String response, const String whichTemp, Temperature *temperature) {
  int tpos = response.indexOf(whichTemp + ":");
  if (tpos != -1) { // This response contains a temperature
    int slashpos = response.indexOf(" /", tpos);
    int spacepos = response.indexOf(" ", slashpos + 1);
    // if match mask T:xxx.xx /xxx.xx
    if (slashpos != -1 && spacepos != -1) {
      String actual = response.substring(tpos + whichTemp.length() + 1, slashpos);
      String target = response.substring(slashpos + 2, spacepos);
      if (isFloat(actual) && isFloat(target)) {
        temperature->actual = actual;
        temperature->target = target;

        return true;
      }
    }
  }

  return false;
}

// Parse temperatures from prusa firmare (sent when heating)
// ok T:32.8 E:0 B:31.8
bool parsePrusaHeatingTemp(const String response, const String whichTemp, Temperature *temperature) {
  int tpos = response.indexOf(whichTemp + ":");
  if (tpos != -1) { // This response contains a temperature
    int spacepos = response.indexOf(" ", tpos);
    if (spacepos == -1)
      spacepos = response.length();
    String actual = response.substring(tpos + whichTemp.length() + 1, spacepos);
    if (isFloat(actual)) {
      temperature->actual = actual;

      return true;
    }
  }

  return false;
}

int8_t parsePrusaHeatingExtruder(const String response) {
  Temperature tmpTemperature;

  return parsePrusaHeatingTemp(response, "E", &tmpTemperature) ? tmpTemperature.actual.toInt() : -1;
}

bool parseTemperatures(const String response) {
  bool tempResponse;

  if (fwExtruders == 1)
    tempResponse = parseTemp(response, "T", &toolTemperature[0]);
  else {
    tempResponse = false;
    for (int t = 0; t < fwExtruders; t++)
      tempResponse |= parseTemp(response, "T" + String(t), &toolTemperature[t]);
  }
  tempResponse |= parseTemp(response, "B", &bedTemperature);
  if (!tempResponse) {
    // Parse Prusa heating temperatures
    int e = parsePrusaHeatingExtruder(response);
    tempResponse = e >= 0 && e < MAX_SUPPORTED_EXTRUDERS && parsePrusaHeatingTemp(response, "T", &toolTemperature[e]);
    tempResponse |= parsePrusaHeatingTemp(response, "B", &bedTemperature);
    }

  return tempResponse;
}

// Parse position responses from printer like
// X:-33.00 Y:-10.00 Z:5.00 E:37.95 Count X:-3300 Y:-1000 Z:2000
inline bool parsePosition(const String response) {
  return response.indexOf("X:") != -1 && response.indexOf("Y:") != -1 &&
         response.indexOf("Z:") != -1 && response.indexOf("E:") != -1;
}

inline void lcd(const String text) {
  commandQueue.push("M117 " + text);
}

inline void playSound() {
  commandQueue.push("M300 S500 P50");
}

inline String getUploadedFilename() {
  return uploadedFullname == "" ? "Unknown" : uploadedFullname.substring(1);
}

void handlePrint() {
  static FileWrapper gcodeFile;
  static float prevM73Completion, prevM532Completion;

  if (isPrinting) {
    const bool abortPrint = (restartPrint || cancelPrint);
    if (abortPrint || !gcodeFile.available()) {
      gcodeFile.close();
      if (fwProgressCap)
        commandQueue.push("M530 S0");
      if (!abortPrint)
        lcd("Complete");
      printPause = false;
      isPrinting = false;
    }
    else if (!printPause && commandQueue.getFreeSlots() > 4) {    // Keep some space for "service" commands
      ++lastPrintedLine;
      String line = gcodeFile.readStringUntil('\n'); // The G-Code line being worked on
      filePos += line.length();
      int pos = line.indexOf(';');
      if (line.length() > 0 && pos != 0 && line[0] != '(' && line[0] != '\r') {
        if (pos != -1)
          line = line.substring(0, pos);
        commandQueue.push(line);
      }

      // Send to printer completion (if supported)
      printCompletion = (float)filePos / uploadedFileSize * 100;
      if (fwBuildPercentCap && printCompletion - prevM73Completion >= 1) {
        commandQueue.push("M73 P" + String((int)printCompletion));
        prevM73Completion = printCompletion;
      }
      if (fwProgressCap && printCompletion - prevM532Completion >= 0.1) {
        commandQueue.push("M532 X" + String((int)(printCompletion * 10) / 10.0));
        prevM532Completion = printCompletion;
      }
    }
  }

  if (!isPrinting && (startPrint || restartPrint)) {
    startPrint = restartPrint = false;

    filePos = 0;
    lastPrintedLine = 0;
    prevM73Completion = prevM532Completion = 0.0;

    gcodeFile = storageFS.open(uploadedFullname);
    if (!gcodeFile)
      lcd("Can't open file");
    else {
      lcd("Printing...");
      playSound();
      printStartTime = millis();
      isPrinting = true;
      if (fwProgressCap) {
        commandQueue.push("M530 S1 L0");
        commandQueue.push("M531 " + getUploadedFilename());
      }
    }
  }
}

void handleUpload(AsyncWebServerRequest *request, String filename, size_t index, uint8_t *data, size_t len, bool final) {
  static FileWrapper file;

  if (!index) {
    lcd("Receiving...");

    if (uploadedFullname != "")
      storageFS.remove(uploadedFullname);     // Remove previous file
    int pos = filename.lastIndexOf("/");
    uploadedFullname = pos == -1 ? "/" + filename : filename.substring(pos);
    if (uploadedFullname.length() > storageFS.getMaxPathLength())
      uploadedFullname = "/cached.gco";   // TODO maybe a different solution
    file = storageFS.open(uploadedFullname, "w"); // create or truncate file
  }

  file.write(data, len);

  if (final) { // upload finished
    file.close();
    uploadedFileSize = index + len;
  }
  else
    uploadedFileSize = 0;
}

int apiJobHandler(JsonObject root) {
  const char* command = root["command"];
  if (command != NULL) {
    if (strcmp(command, "cancel") == 0) {
      if (!isPrinting)
        return 409;
      cancelPrint = true;
    }
    else if (strcmp(command, "start") == 0) {
      if (isPrinting || !printerConnected || uploadedFullname == "")
        return 409;
      startPrint = true;
    }
    else if (strcmp(command, "restart") == 0) {
      if (!printPause)
        return 409;
      restartPrint = true;
    }
    else if (strcmp(command, "pause") == 0) {
      if (!isPrinting)
        return 409;
      const char* action = root["action"];
      if (action == NULL)
        printPause = !printPause;
      else {
        if (strcmp(action, "pause") == 0)
          printPause = true;
        else if (strcmp(action, "resume") == 0)
          printPause = false;
        else if (strcmp(action, "toggle") == 0)
          printPause = !printPause;
      }
    }
  }

  return 204;
}

String M115ExtractString(const String response, const String field) {
  int spos = response.indexOf(field + ":");
  if (spos != -1) {
    spos += field.length() + 1;
    int epos = response.indexOf(':', spos);
    if (epos == -1)
      epos = response.indexOf('\n', spos);
    if (epos == -1)
      return response.substring(spos);
    else {
      while (epos >= spos && response[epos] != ' ' && response[epos] != '\n')
        --epos;
      return response.substring(spos, epos);
    }
  }

  return "";
}

bool M115ExtractBool(const String response, const String field, const bool onErrorValue = false) {
  String result = M115ExtractString(response, field);

  return result == "" ? onErrorValue : (result == "1" ? true : false);
}

inline String getDeviceName() {
  return fwMachineType + " (" + String(ESP.getChipId(), HEX) + ")";
}

void mDNSInit() {
  #ifdef OTA_UPDATES
    MDNS.setInstanceName(getDeviceName().c_str());    // Can't call MDNS.init because it has been already done by 'ArduinoOTA.begin', here I just change instance name
  #else
    if (!MDNS.begin(getDeviceName().c_str()))
      return;
  #endif

  // For Cura WirelessPrint - deprecated in favor of the OctoPrint API
  MDNS.addService("wirelessprint", "tcp", 80);
  MDNS.addServiceTxt("wirelessprint", "tcp", "version", SKETCH_VERSION);

  // OctoPrint API
  // Unfortunately, Slic3r doesn't seem to recognize it
  MDNS.addService("octoprint", "tcp", 80);
  MDNS.addServiceTxt("octoprint", "tcp", "path", "/");
  MDNS.addServiceTxt("octoprint", "tcp", "api", API_VERSION);
  MDNS.addServiceTxt("octoprint", "tcp", "version", SKETCH_VERSION);

  MDNS.addService("http", "tcp", 80);
  MDNS.addServiceTxt("http", "tcp", "path", "/");
  MDNS.addServiceTxt("http", "tcp", "api", API_VERSION);
  MDNS.addServiceTxt("http", "tcp", "version", SKETCH_VERSION);
}

bool detectPrinter() {
  static int printerDetectionState;

  switch (printerDetectionState) {
    case 0:
      // Start printer detection
      serialBaudIndex = 0;
      printerDetectionState = 10;
      break;

    case 10:
      // Initialize baud and send a request to printezr
      Serial.begin(serialBauds[serialBaudIndex]);
      telnetSend("Connecting at " + String(serialBauds[serialBaudIndex]));
      commandQueue.push("\xFFM115"); // M115 - Firmware Info
      printerDetectionState = 20;
      break;

    case 20:
      // Check if there is a printer response
      if (commandQueue.isEmpty()) {
        String value = M115ExtractString(lastReceivedResponse, "MACHINE_TYPE");
        if (value == "") {
          ++serialBaudIndex;
          if (serialBaudIndex < sizeof(serialBauds) / sizeof(serialBauds[0]))
            printerDetectionState = 10;
          else
            printerDetectionState = 0;
        }
        else {
          telnetSend("Connected");

          fwMachineType = value;
          value = M115ExtractString(lastReceivedResponse, "EXTRUDER_COUNT");

          fwExtruders = value == "" ? 1 : min(value.toInt(), (long)MAX_SUPPORTED_EXTRUDERS);
          fwAutoreportTempCap = M115ExtractBool(lastReceivedResponse, "Cap:AUTOREPORT_TEMP");
          fwProgressCap = M115ExtractBool(lastReceivedResponse, "Cap:PROGRESS");
          fwBuildPercentCap = M115ExtractBool(lastReceivedResponse, "Cap:BUILD_PERCENT");

          mDNSInit();

          String text = IpAddress2String(WiFi.localIP()) + " " + storageFS.getActiveFS();
          lcd(text);
          playSound();

          if (fwAutoreportTempCap)
            commandQueue.push(AUTOTEMP_COMMAND + String(TEMPERATURE_REPORT_INTERVAL));   // Start auto report temperatures
          else
            temperatureTimer = millis();
          return true;
        }
      }
      break;
  }

  return false;
}

void initUploadedFilename() {
  FileWrapper dir = storageFS.open("/");
  if (dir) {
    FileWrapper file = dir.openNextFile();
    while (file && file.isDirectory()) {
      file.close();
      file = dir.openNextFile();
    }
    if (file) {
      uploadedFullname = "/" + file.name();
      uploadedFileSize = file.size();
      file.close();
    }
    dir.close();
  }
}

inline String getState() {
  if (!printerConnected)
    return "Discovering printer";
  else if (cancelPrint)
    return "Cancelling";
  else if (printPause)
    return "Paused";
  else if (isPrinting)
    return "Printing";
  else
    return "Operational";
}

inline String stringify(bool value) {
  return value ? "true" : "false";
}

void setup() {
  pinMode(LED_BUILTIN, OUTPUT);     // Initialize the LED_BUILTIN pin as an output

  #ifdef USE_FAST_SD
    storageFS.begin(true);
  #else
    storageFS.begin(false);
  #endif

  for (int t = 0; t < MAX_SUPPORTED_EXTRUDERS; t++)
    toolTemperature[t] = { "0.0", "0.0" };
  bedTemperature = { "0.0", "0.0" };

  // Wait for connection
  setLed(true);
  AsyncWiFiManager wifiManager(&server, &dns);
  // wifiManager.resetSettings();   // Uncomment this to reset the settings on the device, then you will need to reflash with USB and this commented out!
  wifiManager.setDebugOutput(false);  // So that it does not send stuff to the printer that the printer does not understand
  wifiManager.autoConnect("AutoConnectAP");
  setLed(false);

  telnetServer.begin();
  telnetServer.setNoDelay(true);

  if (storageFS.activeSPIFFS())
    server.addHandler(new SPIFFSEditor());

  initUploadedFilename();

  server.onNotFound([](AsyncWebServerRequest * request) {
    telnetSend("404 | Page '" + request->url() + "' not found");
    request->send(404, "text/html", "<h1>Page not found!</h1>");
  });

  // Main page
  server.on("/", HTTP_GET, [](AsyncWebServerRequest * request) {
    String message = "<h1>" + getDeviceName() + "</h1>"
                     "<form enctype=\"multipart/form-data\" action=\"/api/files/local\" method=\"POST\">\n"
                     "<p>You can also print from the command line using curl:</p>\n"
                     "<pre>curl -F \"file=@/path/to/some.gcode\" -F \"print=true\" " + IpAddress2String(WiFi.localIP()) + "/api/files/local</pre>\n"
                     "Choose a file to upload: <input name=\"file\" type=\"file\"/><br/>\n"
                     "<input type=\"checkbox\" name=\"print\" id = \"printImmediately\" value=\"true\" checked>\n"
                     "<label for = \"printImmediately\">Print Immediately</label><br/>\n"
                     "<input type=\"submit\" value=\"Upload\" />\n"
                     "</form>"
    #ifdef OTA_UPDATES
                     "<p><form enctype=\"multipart/form-data\" action=\"/update\" method=\"POST\">\nChoose a firmware file: <input name=\"file\" type=\"file\"/><br/>\n<input type=\"submit\" value=\"Update firmware\" /></p>\n</form>"
    #endif
                     "<p><script>\nfunction startFunction(command) {\n  var xmlhttp = new XMLHttpRequest();\n  xmlhttp.open(\"POST\", \"/api/job\");\n  xmlhttp.setRequestHeader(\"Content-Type\", \"application/json\");\n  xmlhttp.send(JSON.stringify({command:command}));\n}\n</script>\n<button onclick=\"startFunction(\'cancel\')\">Cancel active print</button>\n<button onclick=\"startFunction(\'start\')\">Print last uploaded file</button></p>\n"
                     "<p><a href=\"/download\">Download</a></p>"
                     "<p><a href=\"/info\">Info</a></p>"


                     "<p>WirelessPrinting <a href=\"https://github.com/probonopd/WirelessPrinting/commit/" + SKETCH_VERSION + "\">" + SKETCH_VERSION + "</a></p>";
    request->send(200, "text/html", message);
  });

  // Info page
  server.on("/info", HTTP_GET, [](AsyncWebServerRequest * request) {
    String message = "<pre>"
                     "Free heap: " + String(ESP.getFreeHeap()) + "\n\n"
                     "File system: " + storageFS.getActiveFS() + "\n";
    if (storageFS.isActive()) {
      message += "Filename length limit: " + String(storageFS.getMaxPathLength()) + "\n";
      if (uploadedFullname != "") {
        message += "Uploaded file: " + getUploadedFilename() + "\n"
                   "Uploaded file size: " + String(uploadedFileSize) + "\n";
      }
    }
    message += "\n"
               "Last command sent: " + lastCommandSent + "\n"
               "Last received response: " + lastReceivedResponse + "\n";
    if (printerConnected) {
      message += "\n"
                 "EXTRUDER_COUNT: " + String(fwExtruders) + "\n"
                 "AUTOREPORT_TEMP: " + stringify(fwAutoreportTempCap);
      if (fwAutoreportTempCap)
        message += " Enabled: " + stringify(autoreportTempEnabled);
      message += "\n"
                 "PROGRESS: " + stringify(fwProgressCap) + "\n"
                 "BUILD_PERCENT: " + stringify(fwBuildPercentCap) + "\n";
    }
    message += "</pre>";
    request->send(200, "text/html", message);
  });

  // Download page
  server.on("/download", HTTP_GET, [](AsyncWebServerRequest * request) {
    AsyncWebServerResponse *response = request->beginResponse("application/x-gcode", uploadedFileSize, [](uint8_t *buffer, size_t maxLen, size_t index) -> size_t {
      static size_t downloadBytesLeft;
      static FileWrapper downloadFile;

      if (!index) {
        downloadFile = storageFS.open(uploadedFullname);
        downloadBytesLeft = uploadedFileSize;
      }
      size_t bytes = min(downloadBytesLeft, maxLen);
      bytes = min(bytes, (size_t)2048);
      bytes = downloadFile.read(buffer, bytes);
      downloadBytesLeft -= bytes;
      if (bytes <= 0)
        downloadFile.close();

      return bytes;
    });
    response->addHeader("Content-Disposition", "attachment; filename=\"" + getUploadedFilename()+ "\"");
    request->send(response);    
  });

  server.on("/api/version", HTTP_GET, [](AsyncWebServerRequest * request) {
    // http://docs.octoprint.org/en/master/api/version.html
    request->send(200, "application/json", "{\r\n"
                                           "  \"api\": \"" API_VERSION "\",\r\n"
                                           "  \"server\": \"" VERSION "\"\r\n"
                                           "}");  });

  server.on("/api/connection", HTTP_GET, [](AsyncWebServerRequest * request) {
    // http://docs.octoprint.org/en/master/api/connection.html#get-connection-settings
    request->send(200, "application/json", "{\r\n"
                                           "  \"current\": {\r\n"
                                           "    \"state\": \"" + getState() + "\",\r\n"
                                           "    \"port\": \"Serial\",\r\n"
                                           "    \"baudrate\": " + serialBauds[serialBaudIndex] + ",\r\n"
                                           "    \"printerProfile\": \"Default\"\r\n"
                                           "  },\r\n"
                                           "  \"options\": {\r\n"
                                           "    \"ports\": \"Serial\",\r\n"
                                           "    \"baudrate\": " + serialBauds[serialBaudIndex] + ",\r\n"
                                           "    \"printerProfiles\": \"Default\",\r\n"
                                           "    \"portPreference\": \"Serial\",\r\n"
                                           "    \"baudratePreference\": " + serialBauds[serialBaudIndex] + ",\r\n"
                                           "    \"printerProfilePreference\": \"Default\",\r\n"
                                           "    \"autoconnect\": true\r\n"
                                           "  }\r\n"
                                           "}");
  });

  // Todo: http://docs.octoprint.org/en/master/api/connection.html#post--api-connection

  // File Operations
  // Pending: http://docs.octoprint.org/en/master/api/files.html#retrieve-all-files
  server.on("/api/files", HTTP_GET, [](AsyncWebServerRequest * request) {
    request->send(200, "application/json", "{\r\n"
                                           "  \"files\": {\r\n"
                                           "  }\r\n"
                                           "}");
  });

  // For Slic3r OctoPrint compatibility
  server.on("/api/files/local", HTTP_POST, [](AsyncWebServerRequest * request) {
    // https://docs.octoprint.org/en/master/api/files.html?highlight=api%2Ffiles%2Flocal#upload-file-or-create-folder
    lcd("Received");
    playSound();

    if (request->hasParam("print", true))
      startPrint = printerConnected && !isPrinting && uploadedFullname != "";

    request->send(200, "application/json", "{\r\n"
                                           "  \"files\": {\r\n"
                                           "    \"local\": {\r\n"
                                           "      \"name\": \"" + getUploadedFilename() + "\",\r\n"
                                           "      \"origin\": \"local\"\r\n"
                                           "    }\r\n"
                                           "  },\r\n"
                                           "  \"done\": true\r\n"
                                           "}");
  }, handleUpload);

  server.on("/api/job", HTTP_GET, [](AsyncWebServerRequest * request) {
    // http://docs.octoprint.org/en/master/api/job.html#retrieve-information-about-the-current-job
    int32_t printTime = 0, printTimeLeft = 0;
    if (isPrinting) {
      printTime = (millis() - printStartTime) / 1000;
      printTimeLeft = (printCompletion > 0) ? printTime / printCompletion * (100 - printCompletion) : INT32_MAX;
    }
    request->send(200, "application/json", "{\r\n"
                                           "  \"job\": {\r\n"
                                           "    \"file\": {\r\n"
                                           "      \"name\": \"" + getUploadedFilename() + "\",\r\n"
                                           "      \"origin\": \"local\",\r\n"
                                           "      \"size\": " + String(uploadedFileSize) + ",\r\n"
                                           "      \"date\": " + String(uploadedFileDate) + "\r\n"
                                           "    },\r\n"
                                           //"    \"estimatedPrintTime\": \"" + estimatedPrintTime + "\",\r\n"
                                           "    \"filament\": {\r\n"
                                           //"      \"length\": \"" + filementLength + "\",\r\n"
                                           //"      \"volume\": \"" + filementVolume + "\"\r\n"
                                           "    }\r\n"
                                           "  },\r\n"
                                           "  \"progress\": {\r\n"
                                           "    \"completion\": " + String(printCompletion) + ",\r\n"
                                           "    \"filepos\": " + String(filePos) + ",\r\n"
                                           "    \"printTime\": " + String(printTime) + ",\r\n"
                                           "    \"printTimeLeft\": " + String(printTimeLeft) + "\r\n"
                                           "  },\r\n"
                                           "  \"state\": \"" + getState() + "\"\r\n"
                                           "}");
  });

  server.on("/api/job", HTTP_POST, [](AsyncWebServerRequest *request) {
    // Job commands http://docs.octoprint.org/en/master/api/job.html#issue-a-job-command
    request->send(200, "text/plain", "");
    },
    [](AsyncWebServerRequest *request, const String& filename, size_t index, uint8_t *data, size_t len, bool final) {
      request->send(400, "text/plain", "file not supported");
    },
    [](AsyncWebServerRequest *request, uint8_t *data, size_t len, size_t index, size_t total) {
      static String content;

      if (!index)
        content = "";
      for (int i = 0; i < len; ++i)
        content += (char)data[i];
      if (content.length() >= total) {
        DynamicJsonDocument doc(1024);
        auto error = deserializeJson(doc, content);
        if (error)
          request->send(400, "text/plain", error.c_str());
        else {
          int responseCode = apiJobHandler(doc.as<JsonObject>());
          request->send(responseCode, "text/plain", "");
          content = "";
        }
      }
  });
  
  server.on("/api/settings", HTTP_GET, [](AsyncWebServerRequest * request) {
    // https://github.com/probonopd/WirelessPrinting/issues/30
    // https://github.com/probonopd/WirelessPrinting/issues/18#issuecomment-321927016
    request->send(200, "application/json", "{}");
  });

  server.on("/api/printer", HTTP_GET, [](AsyncWebServerRequest * request) {
    // https://docs.octoprint.org/en/master/api/printer.html#retrieve-the-current-printer-state
    String readyState = stringify(printerConnected);
    String message = "{\r\n"
                     "  \"state\": {\r\n"
                     "    \"text\": \"" + getState() + "\",\r\n"
                     "    \"flags\": {\r\n"
                     "      \"operational\": " + readyState + ",\r\n"
                     "      \"paused\": " + stringify(printPause) + ",\r\n"
                     "      \"printing\": " + stringify(isPrinting) + ",\r\n"
                     "      \"pausing\": false,\r\n"
                     "      \"cancelling\": " + stringify(cancelPrint) + ",\r\n"
                     "      \"sdReady\": false,\r\n"
                     "      \"error\": false,\r\n"
                     "      \"ready\": " + readyState + ",\r\n"
                     "      \"closedOrError\": " + stringify(!printerConnected) + "\r\n"
                     "    }\r\n"
                     "  },\r\n"
                     "  \"temperature\": {\r\n";
    for (int t = 0; t < fwExtruders; ++t) {
      message += "    \"tool" + String(t) + "\": {\r\n"
                 "      \"actual\": " + toolTemperature[t].actual + ",\r\n"
                 "      \"target\": " + toolTemperature[t].target + ",\r\n"
                 "      \"offset\": 0\r\n"
                 "    },\r\n";
    }
    message += "    \"bed\": {\r\n"
               "      \"actual\": " + bedTemperature.actual + ",\r\n"
               "      \"target\": " + bedTemperature.target + ",\r\n"
               "      \"offset\": 0\r\n"
               "    }\r\n"
               "  },\r\n"
               "  \"sd\": {\r\n"
               "    \"ready\": false\r\n"
               "  }\r\n"
               "}";
    request->send(200, "application/json", message);
  });

  server.on("/api/printer/command", HTTP_POST, [](AsyncWebServerRequest *request) {
    // http://docs.octoprint.org/en/master/api/printer.html#send-an-arbitrary-command-to-the-printer
    request->send(200, "text/plain", "");
    },
    [](AsyncWebServerRequest *request, const String& filename, size_t index, uint8_t *data, size_t len, bool final) {
      request->send(400, "text/plain", "file not supported");
    },
    [](AsyncWebServerRequest *request, uint8_t *data, size_t len, size_t index, size_t total) {
      static String content;

      if (!index)
        content = "";
      for (size_t i = 0; i < len; ++i)
        content += (char)data[i];
      if (content.length() >= total) {
        DynamicJsonDocument doc(1024);
        auto error = deserializeJson(doc, content);
        if (error)
          request->send(400, "text/plain", error.c_str());
        else {
          JsonObject root = doc.as<JsonObject>();
          const char* command = root["command"];
          if (command != NULL)
            commandQueue.push(command);
          else {
            JsonArray commands = root["commands"].as<JsonArray>();
            for (JsonVariant command : commands)
              commandQueue.push(String(command.as<String>()));
            }
          request->send(204, "text/plain", "");
        }
        content = "";
      }
  });

  // For legacy PrusaControlWireless - deprecated in favor of the OctoPrint API
  server.on("/print", HTTP_POST, [](AsyncWebServerRequest * request) {
    request->send(200, "text/plain", "Received");
  }, handleUpload);

  // For legacy Cura WirelessPrint - deprecated in favor of the OctoPrint API
  server.on("/api/print", HTTP_POST, [](AsyncWebServerRequest * request) {
    request->send(200, "text/plain", "Received");
  }, handleUpload);
  
  #ifdef OTA_UPDATES  
  // Handling ESP firmware file upload
  // https://github.com/me-no-dev/ESPAsyncWebServer/issues/3#issuecomment-354528317
  // https://gist.github.com/JMishou/60cb762047b735685e8a09cd2eb42a60
  server.on("/update", HTTP_POST, [](AsyncWebServerRequest *request) {
    // the request handler is triggered after the upload has finished... 
    // create the response, add header, and send response
    AsyncWebServerResponse *response = request->beginResponse(200, "text/plain", (Update.hasError())?"FAIL":"OK");
    response->addHeader("Connection", "close");
    response->addHeader("Access-Control-Allow-Origin", "*");
    ESPrestartRequired = true;  // Tell the main loop to restart the ESP
    request->send(response);
  }, [](AsyncWebServerRequest *request, String filename, size_t index, uint8_t *data, size_t len, bool final) {
    //Upload handler chunks in data
    if (!index) { // if index == 0 then this is the first frame of data
      //Serial.printf("UploadStart: %s\n", filename.c_str());
      lcd("Update Start");
      telnetSend("Update Start");
      //Serial.setDebugOutput(true);
      // calculate sketch space required for the update
      uint32_t maxSketchSpace = (ESP.getFreeSketchSpace() - 0x1000) & 0xFFFFF000;
      if (!Update.begin(maxSketchSpace)){ //start with max available size
        //Update.printError(Serial);
        lcd("Update Error0");
        telnetSend("Update Error0");        
      }
      Update.runAsync(true); // tell the updaterClass to run in async mode
    }
    // Write chunked data to the free sketch space
    if (Update.write(data, len) != len) {
        lcd("Update Error1");
        telnetSend("Update Error1");       
    }
//      Update.printError(Serial);
        
    if (final) { // if the final flag is set then this is the last frame of data
      if (Update.end(true)) { //true to set the size to the current progress
//        Serial.printf("Update Success: %u B\nRebooting...\n", index+len);
        lcd("Update Success");
        telnetSend("Update Success");
      }else{
//        Update.printError(Serial);
        lcd("Update Error2");
        telnetSend("Update Error2");
      }
//    Serial.setDebugOutput(false);
    }
  });
  #endif
  
  server.begin();

  #ifdef OTA_UPDATES
    // OTA setup
    ArduinoOTA.setHostname(getDeviceName().c_str());
    #ifdef OTA_PASSWORD
      ArduinoOTA.setPassword(OTA_PASSWORD);
    #endif
    ArduinoOTA.begin();
  #endif
}

void loop() {
  #ifdef OTA_UPDATES
    //****************
    //* OTA handling *
    //****************
    if (ESPrestartRequired) {  // check the flag here to determine if a restart is required
      Serial.printf("Restarting ESP\n\r");
      ESPrestartRequired = false;
      ESP.restart();
    }

    ArduinoOTA.handle();
  #endif

  //********************
  //* Printer handling *
  //********************
  if (!printerConnected)
    printerConnected = detectPrinter();
  else {
    #ifndef OTA_UPDATES
      MDNS.update();    // When OTA is active it's called by 'handle' method
    #endif

    handlePrint();

    if (cancelPrint && !isPrinting) { // Only when cancelPrint has been processed by 'handlePrint'
      cancelPrint = false;
      commandQueue.clear();
      printerUsedBuffer = 0;
      // Apparently we need to decide how to handle this
      // For now using M112 - Emergency Stop
      // http://marlinfw.org/docs/gcode/M112.html
      telnetSend("Should cancel print! This is not working yet");
      commandQueue.push("M112"); // Send to 3D Printer immediately w/o waiting for anything
      //playSound();
      //lcd("Print cancelled");
    }

    if (!autoreportTempEnabled) {
      unsigned long curMillis = millis();
      if ((signed)(temperatureTimer - curMillis) <= 0) {
        commandQueue.push(TEMP_COMMAND);
        temperatureTimer = curMillis + TEMPERATURE_REPORT_INTERVAL * 1000;
      }
    }
  }

  SendCommands();
  ReceiveResponses();


  //*******************
  //* Telnet handling *
  //*******************
  // look for Client connect trial
  if (telnetServer.hasClient() && (!serverClient || !serverClient.connected())) {
    if (serverClient)
      serverClient.stop();

    serverClient = telnetServer.available();
    serverClient.flush();  // clear input buffer, else you get strange characters
  }

  static String telnetCommand;
  while (serverClient && serverClient.available()) {  // get data from Client
    {
    char ch = serverClient.read();
    if (ch == '\r' || ch == '\n') {
      if (telnetCommand.length() > 0) {
        commandQueue.push(telnetCommand);
        telnetCommand = "";
      }
    }
    else
      telnetCommand += ch;
    }
  }
}

inline uint32_t restartSerialTimeout() {
  serialReceiveTimeoutTimer = millis() + KEEPALIVE_INTERVAL;
}

void SendCommands() {
  String command = commandQueue.peekSend();  //gets the next command to be sent
  if (command != "") {
    bool noResponsePending = commandQueue.isAckEmpty();
    if (noResponsePending || printerUsedBuffer < PRINTER_RX_BUFFER_SIZE * 3 / 4) {  // Let's use no more than 75% of printer RX buffer
      if (noResponsePending)
        restartSerialTimeout();   // Receive timeout has to be reset only when sending a command and no pending response is expected
      Serial.println(command);          // Send to 3D Printer
      printerUsedBuffer += command.length();
      lastCommandSent = command;
      commandQueue.popSend();

      telnetSend(">" + command);
    }
  }
}

void ReceiveResponses() {
  static int lineStartPos;
  static String serialResponse;

  while (Serial.available()) {
    char ch = (char)Serial.read();
    if (ch != '\n')
      serialResponse += ch;
    else {
      bool incompleteResponse = false;
      String responseDetail = "";

      if (serialResponse.startsWith("ok", lineStartPos)) {
        if (lastCommandSent.startsWith(TEMP_COMMAND))
          parseTemperatures(serialResponse);
        else if (fwAutoreportTempCap && lastCommandSent.startsWith(AUTOTEMP_COMMAND))
          autoreportTempEnabled = (lastCommandSent[6] != '0');

        unsigned int cmdLen = commandQueue.popAcknowledge().length();     // Go on with next command
        printerUsedBuffer = max(printerUsedBuffer - cmdLen, 0u);
        responseDetail = "ok";
      }
      else {
        if (parseTemperatures(serialResponse))
          responseDetail = "autotemp";
        else if (parsePosition(serialResponse))
          responseDetail = "position";
        else if (serialResponse.startsWith("echo:busy"))
          responseDetail = "busy";
        else if (serialResponse.startsWith("echo: cold extrusion prevented")) {
          // To do: Pause sending gcode, or do something similar
          responseDetail = "cold extrusion";
        }
        else if (serialResponse.startsWith("Error:")) {
          cancelPrint = true;
          responseDetail = "ERROR";
        }
        else {
          incompleteResponse = true;
          responseDetail = "wait more";
        }
      }

      int responseLength = serialResponse.length();
      telnetSend("<" + serialResponse.substring(lineStartPos, responseLength) + "#" + responseDetail + "#");
      if (incompleteResponse)
        lineStartPos = responseLength;
      else {
        lastReceivedResponse = serialResponse;
        lineStartPos = 0;
        serialResponse = "";
      }
      restartSerialTimeout();
    }
  }

  if (!commandQueue.isAckEmpty() && (signed)(serialReceiveTimeoutTimer - millis()) <= 0) {  // Command has been lost by printer, buffer has been freed
    if (printerConnected)
      telnetSend("#TIMEOUT#");
    else
      commandQueue.clear();
    lineStartPos = 0;
    serialResponse = "";
    restartSerialTimeout();
  }
}
