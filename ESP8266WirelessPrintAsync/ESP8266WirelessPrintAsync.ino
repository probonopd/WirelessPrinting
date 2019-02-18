// Required: https://github.com/greiman/SdFat
#include <ArduinoOTA.h>
#if defined(ESP8266)
  #include <ESP8266mDNS.h>
#elif defined(ESP32)
  #include <WiFi.h>
  #include <ESPmDNS.h>
  #include <AsyncTCP.h>
#endif
#include <ArduinoJson.h>    // For implementing (a subset of) the OctoPrint API
#include <DNSServer.h>
#include "StorageFS.h"
#include <ESPAsyncWebServer.h>
#include <ESPAsyncWiFiManager.h>  // https://github.com/alanswx/ESPAsyncWiFiManager/
#include <SPIFFSEditor.h>

#include "CommandQueue.h"

WiFiServer telnetServer(23);
WiFiClient serverClient;

AsyncWebServer server(80);
DNSServer dns;

// Configurable parameters
#define SKETCH_VERSION "2.0"
#define PRINTER_RX_BUFFER_SIZE 0        // This is printer firmware 'RX_BUFFER_SIZE'. If such parameter is unknown please use 0
#define TEMPERATURE_REPORT_INTERVAL 2   // Ask the printer for its temperatures status every 2 seconds
#define MAX_SUPPORTED_EXTRUDERS 6       // Number of supported extruder
#define USE_FAST_SD                     // Use Default fast SD clock, comment if your SD is an old or slow one.
//#define OTA_UPDATES                   // Enable OTA firmware updates, comment if you don't want it (OTA may lead to security issues because someone may load every code on device)
const int serialBauds[] = {  250000};   // Marlin valid bauds (removed very low bauds)

// Information from M115
String fwMachineType = "Unknown";
int fwExtruders = 1;
bool fwAutoreportTempCap, fwProgressCap, fwBuildPercentCap;

String deviceName = "Unknown";
bool hasSD; // will be set true if SD card is detected and usable; otherwise use SPIFFS
bool printerConnected;
bool startPrint, isPrinting, printPause, restartPrint, cancelPrint;
String lastCommandSent, lastReceivedResponse;
long lastPrintedLine;

unsigned int printerUsedBuffer;
unsigned int serialReceiveTimeoutValue;
unsigned long serialReceiveTimeoutTimer;

unsigned long temperatureTimer;

String uploadedFullname;
size_t uploadedFileSize, filePos;

unsigned long printStartTime;
float printCompletion;

struct Temperature {
  String actual, target;
};

// Variables for printer status reporting
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
  if (serverClient && serverClient.connected()) { // send data to telnet client if connected
    serverClient.println(line);
  }
}

// Parse temperatures from printer responses like
// ok T:32.8 /0.0 B:31.8 /0.0 T0:32.8 /0.0 @:0 B@:0
bool parseTemp(const String response, const String whichTemp, Temperature *temperature) {
  int tpos = response.indexOf(whichTemp + ":");
  if (tpos != -1) { // This response contains a temperature
    int slashpos = response.indexOf(" /", tpos);
    int spacepos = response.indexOf(" ", slashpos + 1);
    // if match mask T:xxx.xx /xxx.xx
    if (spacepos - tpos < 17) {
      temperature->actual = response.substring(tpos + whichTemp.length() + 1, slashpos);
      temperature->target = response.substring(slashpos + 2, spacepos);

      return true;
    }
  }

  return false;
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

  return tempResponse;
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

int apiPrinterCommandHandler(const uint8_t* data) {
  DynamicJsonBuffer jsonBuffer;
  JsonObject& root = jsonBuffer.parseObject(data);
  if (root.success()) {
    if (root.containsKey("command")) {
      telnetSend(root["command"]);
      String command = root["command"].asString();
      commandQueue.push(command);
    }
  }
  else if (root.containsKey("commands")) {
    JsonArray& node = root["commands"];
    for (JsonArray::iterator item = node.begin(); item != node.end(); ++item)
      commandQueue.push(item->as<char*>());
  }

  return 204;
}

int apiJobHandler(const uint8_t* data) {
  DynamicJsonBuffer jsonBuffer;
  JsonObject& root = jsonBuffer.parseObject(data);
  if (root.success() && root.containsKey("command")) {
    telnetSend(root["command"]);
    String command = root["command"].asString();
    if (command == "cancel") {
      if (!isPrinting)
        return 409;
      cancelPrint = true;
    }
    else if (command == "start") {
      if (isPrinting || !printerConnected || uploadedFullname == "")
        return 409;
      startPrint = true;
    }
    else if (command == "restart") {
      if (!printPause)
        return 409;
      restartPrint = true;
    }
    else if (command == "pause") {
      if (!isPrinting)
        return 409;
      if (!root.containsKey("action"))
        printPause = !printPause;
      else {
        telnetSend(root["action"]);
        String action = root["action"].asString();
        if (action == "pause")
          printPause = true;
        else if (action == "resume")
          printPause = false;
        else if (action == "toggle")
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

void mDNSInit() {
  char output[9];
  itoa(ESP.getChipId(), output, 16);
  String chipID = String(output);
  deviceName = fwMachineType + " (" + chipID + ")";

  if (MDNS.begin(deviceName.c_str())) {
    // For Cura WirelessPrint - deprecated in favor of the OctoPrint API
    MDNS.addService("wirelessprint", "tcp", 80);
    MDNS.addServiceTxt("wirelessprint", "tcp", "version", SKETCH_VERSION);

    // OctoPrint API
    // Unfortunately, Slic3r doesn't seem to recognize it
    MDNS.addService("octoprint", "tcp", 80);
    MDNS.addServiceTxt("octoprint", "tcp", "path", "/");
    MDNS.addServiceTxt("octoprint", "tcp", "api", "0.1");
    MDNS.addServiceTxt("octoprint", "tcp", "version", "1.2.10");

    // For compatibility with Slic3r
    // Unfortunately, Slic3r doesn't seem to recognize it either. Library bug?
    MDNS.addService("http", "tcp", 80);
    MDNS.addServiceTxt("http", "tcp", "path", "/");
    MDNS.addServiceTxt("http", "tcp", "api", "0.1");
    MDNS.addServiceTxt("http", "tcp", "version", "1.2.10");
  }

  MDNS.addService("http", "tcp", 80);
}

bool detectPrinter() {
  static int printerDetectionState, serialBaudIndex;

  switch (printerDetectionState) {
    case 0:
      // Start printer detection
      serialBaudIndex = 0;
      serialReceiveTimeoutValue = 1000;
      printerDetectionState = 10;
      break;

    case 10:
      // Initialize baud and send a request to printezr
      Serial.begin(serialBauds[serialBaudIndex]);
      telnetSend("Connecting at " + String(serialBauds[serialBaudIndex]));
      commandQueue.push("M115"); // M115 - Firmware Info
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
          serialReceiveTimeoutValue = 10000; // Set serial timeout to a safer value (TODO check if it's really needed)

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
            commandQueue.push("M155 S" + String(TEMPERATURE_REPORT_INTERVAL));   // Start auto report temperatures
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

  if (!hasSD)
    server.addHandler(new SPIFFSEditor());

  initUploadedFilename();

  server.onNotFound([](AsyncWebServerRequest * request) {
    telnetSend("404 | Page '" + request->url() + "' not found\r\n");
    request->send(404, "text/html", "<h1>Page not found!</h1>");
  });

  server.on("/", HTTP_GET, [](AsyncWebServerRequest * request) {
    String message = "<h1>" + deviceName + "</h1>"
                     "<form enctype=\"multipart/form-data\" action=\"/api/files/local\" method=\"POST\">\n"
                     "<p>You can also print from the command line using curl:</p>\n"
                     "<pre>curl -F \"file=@/path/to/some.gcode\" " + IpAddress2String(WiFi.localIP()) + "/api/files/local</pre>\n"
                     "Choose a file to upload: <input name=\"file\" type=\"file\"/><br/>\n"
                     "<input type=\"submit\" value=\"Upload\" />\n"
                     "</form>"
                     "<p><a href=\"/download\">Download</a></p>";
    request->send(200, "text/html", message);
  });

  server.on("/info", HTTP_GET, [](AsyncWebServerRequest * request) {
    String message = "<pre>"
                     "Free heap: " + String(ESP.getFreeHeap()) + "\n\n"
                     "File system: " + storageFS.getActiveFS() + "\n"
                     "Filename length limit: " + String(storageFS.getMaxPathLength()) + "\n";
    if (uploadedFullname != "") {
      message += "Uploaded file: " + getUploadedFilename() + "\n"
                 "Uploaded file size: " + String(uploadedFileSize) + "\n";
    }
    message += "\n"
               "Last command sent: " + lastCommandSent + "\n"
               "Last received response: " + lastReceivedResponse + "\n"
               "</pre>";

    request->send(200, "text/html", message);
  });

  // For Slic3r OctoPrint compatibility
  server.on("/api/files/local", HTTP_POST, [](AsyncWebServerRequest * request) {
    // http://docs.octoprint.org/en/master/api/files.html#upload-response
    playSound();
    lcd("Receiving...");

    if (!request->hasParam("file", true, true))
      lcd("Needs PR #192");   // Cura needs https://github.com/me-no-dev/ESPAsyncWebServer/pull/192

    if (request->hasParam("print", true))
      startPrint = printerConnected && !isPrinting && uploadedFullname != "";

    request->send(200, "application/json", "{\r\n"
                                           "  \"files\": {\r\n"
                                           "    \"local\": {\r\n"
                                           "      \"name\": \"" + getUploadedFilename() + "\",\r\n"
                                           "      \"size\": \"" + String(uploadedFileSize) + "\",\r\n"
                                           "      \"origin\": \"local\"\r\n"
                                           "    }\r\n"
                                           "  },\r\n"
                                           "  \"done\": true\r\n"
                                           "}\r\n");
  }, handleUpload);

  // For Cura 2.6.0 OctoPrintPlugin compatibility
  // Must be valid JSON
  // http://docs.octoprint.org/en/master/api
  // TODO: Fill with values that actually make sense; currently this is enough for Cura 2.6.0 not to crash

  // Poor Man's JSON:
  // https://jsonformatter.curiousconcept.com/
  // https://www.freeformatter.com/json-escape.html

  server.on("/api/job", HTTP_GET, [](AsyncWebServerRequest * request) {
    // http://docs.octoprint.org/en/master/api/datamodel.html#sec-api-datamodel-jobs-job
    int printTime = 0, printTimeLeft = 0;
    if (isPrinting) {
      printTime = (millis() - printStartTime) / 1000;
      printTimeLeft = printTimeLeft / printCompletion * (100 - printCompletion);
    }
    request->send(200, "application/json", "{\r\n"
                                           "  \"job\": {\r\n"
                                           "    \"file\": {\r\n"
                                           "      \"name\": \"" + getUploadedFilename() + "\",\r\n"
                                           "      \"size\": " + String(uploadedFileSize) + ",\r\n"
                                           "      \"date\": 1378847754,\r\n"
                                           "      \"origin\": \"local\"\r\n"
                                           "    }\r\n"
                                           "  },\r\n"
                                           "  \"progress\": {\r\n"
                                           "    \"completion\": " + String(printCompletion) + ",\r\n"
                                           "    \"filepos\": " + String(filePos) + ",\r\n"
                                           "    \"printTime\": " + String(printTime) + ",\r\n"
                                           "    \"printTimeLeft\": " + String(printTimeLeft) + "\r\n"
                                           "  }\r\n"
                                           "}");
  });

  server.on("/api/settings", HTTP_GET, [](AsyncWebServerRequest * request) {
    // https://github.com/probonopd/WirelessPrinting/issues/30
    // https://github.com/probonopd/WirelessPrinting/issues/18#issuecomment-321927016
    request->send(200, "application/json", "{}");
  });

  server.on("/api/printer", HTTP_GET, [](AsyncWebServerRequest * request) {
    // http://docs.octoprint.org/en/master/api/datamodel.html#printer-state
    String sdReadyState = String(hasSD ? "true" : "false");
    String readyState = String(printerConnected ? "true" : "false");
    String message = "{\r\n"
                     "  \"state\": {\r\n"
                     "    \"text\": \"" + String(!printerConnected ? "Discovering printer" : isPrinting ? "Printing" : "Operational") + "\",\r\n"
                     "    \"flags\": {\r\n"
                     "      \"operational\": " + readyState + ",\r\n"
                     "      \"paused\": " + String(printPause ? "true" : "false") + ",\r\n"
                     "      \"printing\": " + String(isPrinting ? "true" : "false") + ",\r\n"
                     "      \"sdReady\": " + sdReadyState + ",\r\n"
                     "      \"error\": false,\r\n"
                     "      \"ready\": " + readyState + ",\r\n"
                     "      \"closedOrError\": false\r\n"
                     "    }\r\n"
                     "  },\r\n"
                     "  \"temperature\": {\r\n";
    for (int t = 0; t < fwExtruders; t++) {
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
               "    \"ready\": " + sdReadyState + "\r\n"
               "  }\r\n"
               "}";
    request->send(200, "application/json", message);
  });

  // Parse POST JSON data, https://github.com/me-no-dev/ESPAsyncWebServer/issues/195
  server.onRequestBody([](AsyncWebServerRequest * request, uint8_t *data, size_t len, size_t index, size_t total) {

    int returnCode;
    if (request->url() == "/api/printer/command") {
      // http://docs.octoprint.org/en/master/api/printer.html#send-an-arbitrary-command-to-the-printer
      returnCode = apiPrinterCommandHandler(data);
    }
    else if (request->url() == "/api/job") {
      // http://docs.octoprint.org/en/master/api/job.html
      returnCode = apiJobHandler(data);
    }
    else
      returnCode = 204;

    request->send(returnCode);
  });

  // For legacy PrusaControlWireless - deprecated in favor of the OctoPrint API
  server.on("/print", HTTP_POST, [](AsyncWebServerRequest * request) {
    request->send(200, "text/plain", "Received");
  }, handleUpload);

  // For legacy Cura WirelessPrint - deprecated in favor of the OctoPrint API
  server.on("/api/print", HTTP_POST, [](AsyncWebServerRequest * request) {
    request->send(200, "text/plain", "Received");
  }, handleUpload);

  server.begin();

  #ifdef OTA_UPDATES
    ArduinoOTA.begin();
  #endif
}

void loop() {
  #ifdef OTA_UPDATES
    ArduinoOTA.handle();
  #endif

  // look for Client connect trial
  if (telnetServer.hasClient() && (!serverClient || !serverClient.connected())) {
    if (serverClient)
      serverClient.stop();

    serverClient = telnetServer.available();
    serverClient.flush();  // clear input buffer, else you get strange characters
  }

  if (!printerConnected)
    printerConnected = detectPrinter();
  else {
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

    if (!fwAutoreportTempCap) {
      unsigned long curMillis = millis();
      if (curMillis - temperatureTimer >= TEMPERATURE_REPORT_INTERVAL * 1000) {
        commandQueue.push("M105");
        temperatureTimer = curMillis;
      }
    }
  }

  SendCommands();
  ReceiveResponses();

  while (serverClient && serverClient.available())  // get data from Client
    Serial.write(serverClient.read());
}

void SendCommands() {
  String command = commandQueue.peekSend();
  if (command != "") {
    bool noResponsePending = commandQueue.isAckEmpty();
    if (noResponsePending || printerUsedBuffer < PRINTER_RX_BUFFER_SIZE * 3 / 4) {  // Let's use no more than 75% of printer RX buffer
      if (noResponsePending)
        resetSerialReceiveTimeout();    // Receive timeout has to be reset only when sending a command and no pending response is expected
      Serial.println(command);   // Send to 3D Printer
      printerUsedBuffer += command.length();
      lastCommandSent = command;
      commandQueue.popSend();
      telnetSend("> " + command);
    }
  }
}

inline void resetSerialReceiveTimeout() {
  serialReceiveTimeoutTimer = millis() + serialReceiveTimeoutValue;
}

void ReceiveResponses() {
  static int lineStartPos;
  static String serialResponse;

  if (serialReceiveTimeoutTimer - millis() <= 0) {
    lineStartPos = 0;
    serialResponse = "";

    printerUsedBuffer -= commandQueue.popAcknowledge().length();  // Command has been processed by printer, buffer has been freed

    telnetSend("< #TIMEOUT#");
  }
  else {
    while (Serial.available()) {
      char ch = (char)Serial.read();
      serialResponse += ch;
      if (ch == '\n') {
        if (serialResponse.startsWith("ok", lineStartPos)) {
          if (!parseTemperatures(lastReceivedResponse) || lastCommandSent == "M105") {
            lastReceivedResponse = serialResponse;
            lineStartPos = 0;
            serialResponse = "";

            printerUsedBuffer -= commandQueue.popAcknowledge().length();  // Command has been processed by printer, buffer has been freed

            telnetSend("< " + lastReceivedResponse + "\r\n  " + millis() + "\r\n  free heap RAM: " + ESP.getFreeHeap() + "\r\n");

            resetSerialReceiveTimeout();
          }
        }
        else
          lineStartPos = serialResponse.length();
      }
    }
  }
}
