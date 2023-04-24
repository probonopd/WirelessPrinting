// Required: https://github.com/greiman/SdFat
#include "USBSerial.h"
#include "GCode.h"
#include <esp_log.h>
#include <Arduino.h>
#include <ArduinoOTA.h>
#if defined(ESP8266)
  #include <ESP8266mDNS.h>        // https://github.com/esp8266/Arduino/tree/master/libraries/ESP8266mDNS
#elif defined(ESP32)
  #include <ESPmDNS.h>
  #include <Update.h>
  #include <Hash.h>
#endif
#include <ArduinoJson.h>          // https://github.com/bblanchon/ArduinoJson (for implementing a subset of the OctoPrint API)
#include <DNSServer.h>
#include "StorageFS.h"
#include <ESPAsyncWebServer.h>    // https://github.com/me-no-dev/ESPAsyncWebServer
#include <ESPAsyncWiFiManager.h>  // https://github.com/alanswx/ESPAsyncWiFiManager/
// #include <AsyncElegantOTA.h>      // https://github.com/ayushsharma82/AsyncElegantOTA
#include <SPIFFSEditor.h>

#include "CommandQueue.h"

#include <NeoPixelBus.h>

#if defined(USB_HOST_SERIAL)
#include "usb/cdc_acm_host.h"
#include "usb/vcp_ch34x.hpp"
#include "usb/vcp_cp210x.hpp"
#include "usb/vcp_ftdi.hpp"
#include "usb/vcp.hpp"
#include "usb/usb_host.h"
#define SERIAL_RESPONSE_SZ 256
#endif // defined(USB_HOST_SERIAL)


const uint16_t PixelCount = 20; // this example assumes 4 pixels, making it smaller will cause a failure
const uint8_t PixelPin = 2;  // make sure to set this to the correct pin, ignored for ESP8266 (there it is GPIO2 = D4)
#define colorSaturation 255
RgbColor red(colorSaturation, 0, 0);
RgbColor green(0, colorSaturation, 0);
RgbColor blue(0, 0, colorSaturation);
RgbColor white(colorSaturation);
RgbColor black(0);

#if defined(ESP8266)
  NeoPixelBus<NeoGrbFeature, NeoEsp8266Uart1Ws2813Method> strip(PixelCount); // ESP8266 always uses GPIO2 = D4
#elif defined(ESP32)
  NeoPixelBus<NeoGrbFeature, Neo800KbpsMethod> strip(PixelCount, PixelPin);
#endif

// On ESP8266 use the normal Serial() for now, but name it PrinterSerial for compatibility with ESP32
// On ESP32, use Serial1 (rather than the normal Serial0 which prints stuff during boot that confuses the printer)
#ifdef ESP8266
#define PrinterSerial Serial
#endif
#ifdef ESP32
HardwareSerial PrinterSerial(1);
#endif

WiFiServer telnetServer(23);
WiFiClient serverClient;

AsyncWebServer server(80);
DNSServer dns;

// Configurable parameters
#define SKETCH_VERSION "2.x-localbuild" // Gets inserted at build time by .travis.yml
#define USE_FAST_SD                     // Use Default fast SD clock, comment if your SD is an old or slow one.
//#define OTA_UPDATES                     // Enable OTA firmware updates, comment if you don't want it (OTA may lead to security issues because someone may load any code on device)
//#define OTA_PASSWORD ""               // Uncomment to protect OTA updates and assign a password (inside "")
#define REPEAT_M115_TIMES 1             // M115 retries with same baud (MAX 255)
#define PRINTER_RX_BUFFER_SIZE 0        // This is printer firmware 'RX_BUFFER_SIZE'. If such parameter is unknown please use 0
#define TEMPERATURE_REPORT_INTERVAL 2   // Ask the printer for its temperatures status every 2 seconds
#define KEEPALIVE_INTERVAL 2500         // Marlin defaults to 2 seconds, get a little of margin
const uint32_t serialBauds[] = { 115200, 250000, 57600 };    // Marlin valid bauds (removed very low bauds; roughly ordered by popularity to speed things up)

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
String lastCommandSent;
char lastReceivedResponse[SERIAL_RESPONSE_SZ];
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

uint32_t temperatureTimer;
struct Temperature {
  String actual, target;
};
Temperature toolTemperature[MAX_SUPPORTED_EXTRUDERS];
Temperature bedTemperature;

bool parseTemperatures(const char* r) {
  float actual, target;
  for (int t = 0; t < fwExtruders; t++) {
    String whichTemp = "T";
    if (fwExtruders != 1) {
      whichTemp += String(t);
    }
    if (parseTemp(r, whichTemp.c_str(), &actual, &target)) {
      toolTemperature[t].actual = String(actual);
      toolTemperature[t].target = String(target);
    } else {
      return false;
    }
  }

  if (parseTemp(r, "B", &actual, &target)) {
    bedTemperature.actual = String(actual);
    bedTemperature.target = String(target);
  } else {
    return false;
  }

  /*
  TODO
  if (!tempResponse) {
    // Parse Prusa heating temperatures
    int e = parsePrusaHeatingExtruder(response);
    tempResponse = e >= 0 && e < MAX_SUPPORTED_EXTRUDERS && parsePrusaHeatingTemp(response, "T", &toolTemperature[e]);
    tempResponse |= parsePrusaHeatingTemp(response, "B", &bedTemperature);
  }
  */
  return true;
}

// https://forum.arduino.cc/index.php?topic=228884.msg2670971#msg2670971
inline String IpAddress2String(const IPAddress& ipAddress) {
  return String(ipAddress[0]) + "." +
         String(ipAddress[1]) + "." +
         String(ipAddress[2]) + "." +
         String(ipAddress[3]);
}

inline void setLed(const bool status) {
  #if defined(LED_BUILTIN)
    digitalWrite(LED_BUILTIN, status ? LOW : HIGH);   // Note: LOW turn the LED on
  #endif
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
    if (!gcodeFile) {
      log_e("Can't open file %s", uploadedFullname.c_str());
      lcd("Can't open file");
    } else {
      log_i("Printing %s", uploadedFullname.c_str());
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

int apiJobHandler(JsonObject root, char* detail) {
  const char* command = root["command"];
  if (command != NULL) {
    if (strcmp(command, "cancel") == 0) {
      if (!isPrinting) {
        sprintf(detail, "not printing");
        return 409;
      }
      cancelPrint = true;
    }
    else if (strcmp(command, "start") == 0) {
      if (isPrinting || !printerConnected || uploadedFullname == "") {
        sprintf(detail, "printing=%d connected=%d uploaded=\"%s\"", isPrinting, printerConnected, uploadedFullname.c_str()); // TODO prevent buffer overflow
        return 409;
      }
      startPrint = true;
    }
    else if (strcmp(command, "restart") == 0) {
      if (!printPause) {
        sprintf(detail, "not paused");
        return 409;
      }
      restartPrint = true;
    }
    else if (strcmp(command, "pause") == 0) {
      if (!isPrinting) {
        sprintf(detail, "not printing");
        return 409;
      }
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
  sprintf(detail, "ok");
  return 204;
}

inline String getDeviceName() {
  #if defined(ESP8266)
    return fwMachineType + " (" + String(ESP.getChipId(), HEX) + ")";
  #elif defined(ESP32)
    uint64_t chipid = ESP.getEfuseMac();
    return fwMachineType + " (" + String((uint16_t)(chipid >> 32), HEX) + String((uint32_t)chipid, HEX) + ")";
  #else
    #error Unimplemented chip!
  #endif
}

inline String getDeviceId() {
  #if defined(ESP8266)
    return String(ESP.getChipId(), HEX);
  #elif defined(ESP32)
    uint64_t chipid = ESP.getEfuseMac();
    return String((uint16_t)(chipid >> 32), HEX) + String((uint32_t)chipid, HEX);
  #else
    #error Unimplemented chip!
  #endif
}

void mDNSInit() {
  #ifdef OTA_UPDATES
    MDNS.setInstanceName(getDeviceId().c_str());    // Can't call MDNS.init because it has been already done by 'ArduinoOTA.begin', here I just change instance name
  #else
    if (!MDNS.begin(getDeviceId().c_str()))
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
  static byte nM115;

  switch (printerDetectionState) {
    case 0:
      // Start printer detection
      log_i("Start printer detection");
      serialBaudIndex = 0;
      printerDetectionState = 10;
      break;

    case 10:
      // Initialize baud and send a request to printezr
      #ifndef USB_HOST_SERIAL
      #ifdef ESP8266
      PrinterSerial.begin(serialBauds[serialBaudIndex]); // See note above; we have actually renamed Serial to Serial1
      #endif
      #ifdef ESP32
      PrinterSerial.begin(serialBauds[serialBaudIndex], SERIAL_8N1, 32, 33); // gpio32 = rx, gpio33 = tx
      #endif
      telnetSend("Connecting at " + String(serialBauds[serialBaudIndex]));
      #endif // USB_HOST_SERIAL
      commandQueue.push("M115"); // M115 - Firmware Info
      printerDetectionState = 20;
      break;

    case 20:
      // Check if there is a printer response
      char buf[32];
      memset(buf, 0, 32);
      if (commandQueue.isEmpty()) {
        if (!M115ExtractString(buf, lastReceivedResponse, "MACHINE_TYPE")) {
          log_i("Empty MACHINE_TYPE, retrying with different baud rate (lastRecv %s)", lastReceivedResponse);
          if (nM115++ >= REPEAT_M115_TIMES) {
            nM115 = 0;
            ++serialBaudIndex;
            if (serialBaudIndex < sizeof(serialBauds) / sizeof(serialBauds[0]))
              printerDetectionState = 10;
            else
              printerDetectionState = 0;   
          } 
          else
            printerDetectionState = 10;      
        }
        else {
          //printerConnected = true;
          telnetSend("Connected");

          fwMachineType = buf;
          log_i("Printer detected: '%s'", fwMachineType);
          if (M115ExtractString(buf, lastReceivedResponse, "EXTRUDER_COUNT")) {
            fwExtruders = min(atoi(buf), MAX_SUPPORTED_EXTRUDERS);
          } else {
            fwExtruders = 1;
          }
          fwAutoreportTempCap = M115ExtractBool(lastReceivedResponse, "Cap:AUTOREPORT_TEMP");
          fwProgressCap = M115ExtractBool(lastReceivedResponse, "Cap:PROGRESS");
          fwBuildPercentCap = M115ExtractBool(lastReceivedResponse, "Cap:BUILD_PERCENT");

          mDNSInit();

          String text = IpAddress2String(WiFi.localIP()) + " " + storageFS.getActiveFS();
          lcd(text);
          playSound();

          if (fwAutoreportTempCap) {
            commandQueue.push(AUTOTEMP_COMMAND + String(TEMPERATURE_REPORT_INTERVAL));   // Start auto report temperatures
          } else {
            temperatureTimer = millis();
          }
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

#if defined(USB_HOST_SERIAL)
#define USB_BUFSZ 1024
char usb_buf[USB_BUFSZ];
size_t usb_buf_idx = 0;
size_t usb_read_idx = 0;
bool handle_usb_rx(const uint8_t* data, size_t data_len, void *arg) {
  //log_i("USB recv: %.*s", data_len, data);
  for (int i = 0; i < data_len; i++) {
    usb_buf[usb_buf_idx] = data[i];
    usb_buf_idx = (usb_buf_idx + 1) % USB_BUFSZ;
    assert(usb_buf_idx != usb_read_idx); // Overflow
  }
  return true;
}
#endif //USB_HOST_SERIAL

void setup() {
  #if defined(LED_BUILTIN)
    pinMode(LED_BUILTIN, OUTPUT);     // Initialize the LED_BUILTIN pin as an output
  #endif

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
  #ifdef OTA_UPDATES
    AsyncElegantOTA.begin(&server);
  #endif
  AsyncWiFiManager wifiManager(&server, &dns);
  // wifiManager.resetSettings();   // Uncomment this to reset the settings on the device, then you will need to reflash with USB and this commented out!
  wifiManager.setDebugOutput(false); 
  wifiManager.autoConnect("AutoConnectAP");
  setLed(false);


  telnetServer.begin();
  telnetServer.setNoDelay(true);

  if (storageFS.activeSPIFFS()) {
    #if defined(ESP8266)
      server.addHandler(new SPIFFSEditor());
    #elif defined(ESP32)
      server.addHandler(new SPIFFSEditor(SPIFFS));
    #else
      #error Unsupported SOC
    #endif
  }

  initUploadedFilename();

  server.onNotFound([](AsyncWebServerRequest * request) {
    telnetSend("404 | Page '" + request->url() + "' not found");
    request->send(404, "text/html", "<h1>Page not found!</h1>");
  });

  // Main page
  server.on("/", HTTP_GET, [](AsyncWebServerRequest * request) {
      String uploadedName = uploadedFullname;
  uploadedName.replace("/", "");
    String message = "<h1>" + getDeviceName() + "</h1>"
                     "<form enctype=\"multipart/form-data\" action=\"/api/files/local\" method=\"POST\">\n"
                     "<p>You can also print from the command line using curl:</p>\n"
                     "<pre>curl -F \"file=@/path/to/some.gcode\" -F \"print=true\" " + IpAddress2String(WiFi.localIP()) + "/api/files/local</pre>\n"
                     "Choose a file to upload: <input name=\"file\" type=\"file\" accept=\".gcode,.GCODE,.gco,.GCO\"/><br/>\n"
                     "<input type=\"checkbox\" name=\"print\" id = \"printImmediately\" value=\"true\" checked>\n"
                     "<label for = \"printImmediately\">Print Immediately</label><br/>\n"
                     "<input type=\"submit\" value=\"Upload\" />\n"
                     "</form>"
                     "<p><script>\nfunction startFunction(command) {\n  var xmlhttp = new XMLHttpRequest();\n  xmlhttp.open(\"POST\", \"/api/job\");\n  xmlhttp.setRequestHeader(\"Content-Type\", \"application/json\");\n  xmlhttp.send(JSON.stringify({command:command}));\n}\n</script>\n<button onclick=\"startFunction(\'cancel\')\">Cancel active print</button>\n<button onclick=\"startFunction(\'start\')\">Print " + uploadedName + "</button></p>\n"
                     "<p><a href=\"/download\">Download " + uploadedName + "</a></p>"
                     "<p><a href=\"/info\">Info</a></p>"
                     "<hr>"
                     "<p>WirelessPrinting <a href=\"https://github.com/probonopd/WirelessPrinting/commit/" + SKETCH_VERSION + "\">" + SKETCH_VERSION + "</a></p>\n"
                    #ifdef OTA_UPDATES
                      "<p>OTA Update Device: <a href=\"/update\">Click Here</a></p>"
                    #endif
                     ;
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

  server.on("/api/login", HTTP_POST, [](AsyncWebServerRequest * request) {
    // https://docs.octoprint.org/en/master/api/general.html#post--api-login
    // https://github.com/fieldOfView/Cura-OctoPrintPlugin/issues/155#issuecomment-596109663
    request->send(200, "application/json", "{}");  });

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

    // We are not using
    // if (request->hasParam("print", true))
    // due to https://github.com/fieldOfView/Cura-OctoPrintPlugin/issues/156
    
    startPrint = printerConnected && !isPrinting && uploadedFullname != "";

    // OctoPrint sends 201 here; https://github.com/fieldOfView/Cura-OctoPrintPlugin/issues/155#issuecomment-596110996
    request->send(201, "application/json", "{\r\n"
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
          char detail[64];
          int responseCode = apiJobHandler(doc.as<JsonObject>(), detail);
          request->send(responseCode, "text/plain", detail);
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

  server.begin();

  #ifdef OTA_UPDATES
    // OTA setup
    ArduinoOTA.setHostname(getDeviceId().c_str());
    #ifdef OTA_PASSWORD
      ArduinoOTA.setPassword(OTA_PASSWORD);
    #endif
    ArduinoOTA.begin();
  #endif

  #if defined(USB_HOST_SERIAL)
  USBHost::setup(&handle_usb_rx);
  #endif // USB_HOST_SERIAL
}

inline void restartSerialTimeout() {
  serialReceiveTimeoutTimer = millis() + KEEPALIVE_INTERVAL;
}

void SendCommands() {
  String command = commandQueue.peekSend();  //gets the next command to be sent
  if (command != "") {
    bool noResponsePending = commandQueue.isAckEmpty();
    if (noResponsePending || printerUsedBuffer < PRINTER_RX_BUFFER_SIZE * 3 / 4) {  // Let's use no more than 75% of printer RX buffer
      if (noResponsePending)
        restartSerialTimeout();   // Receive timeout has to be reset only when sending a command and no pending response is expected

      #if defined(USB_HOST_SERIAL)
      char buf[64]; // TODO Const
      snprintf(buf, 64, "%s\n", command.c_str());
      USBHost::send((uint8_t*)buf, command.length()+1); // +1 for newline
      #else
      PrinterSerial.println(command);          // Send to 3D Printer
      #endif
      printerUsedBuffer += command.length();
      lastCommandSent = command;
      commandQueue.popSend();

      telnetSend(">" + command);
    }
  }
}

void ReceiveResponses() {
  static int lineStartPos;
  static char serialResponse[SERIAL_RESPONSE_SZ];
  int responseLength = 0;
#if defined(USB_HOST_SERIAL)
  while (usb_read_idx != usb_buf_idx) {
    char ch = (char)usb_buf[usb_read_idx];
    //log_printf("%c", ch);
    usb_read_idx = (usb_read_idx + 1) % USB_BUFSZ;
    serialResponse[lineStartPos + (responseLength++)] = ch;

    if (ch == '\n') {
      bool incompleteResponse = false;
      String responseDetail = "";

      if (hasPrefix("ok", serialResponse+lineStartPos)) {
        if (hasPrefix(TEMP_COMMAND, lastCommandSent.c_str()))
          parseTemperatures(serialResponse);
        else if (fwAutoreportTempCap && hasPrefix(AUTOTEMP_COMMAND, lastCommandSent.c_str()))
          autoreportTempEnabled = (lastCommandSent[6] != '0');

        unsigned int cmdLen = commandQueue.popAcknowledge().length();     // Go on with next command
        printerUsedBuffer = max(printerUsedBuffer - cmdLen, 0u);
        responseDetail = "ok";
      }
      else if (printerConnected) {
        if (parseTemperatures(serialResponse))
          responseDetail = "autotemp";
        else if (parsePosition(serialResponse))
          responseDetail = "position";
        else if (hasPrefix("echo:busy", serialResponse))
          responseDetail = "busy";
        else if (hasPrefix("echo: cold extrusion prevented", serialResponse)) {
          // To do: Pause sending gcode, or do something similar
          responseDetail = "cold extrusion";
        }
        else if (hasPrefix("Error:", serialResponse)) {
          cancelPrint = true;
          responseDetail = "ERROR";
        }
        else {
          incompleteResponse = true;
          responseDetail = "wait more";
        }
      } else {
          incompleteResponse = true;
          responseDetail = "discovering";
      }
      
      log_i("RECV: incomplete=%d, detail=%s, rep=%s", incompleteResponse, responseDetail, serialResponse);
      // TODO: telnetSend("<" + serialResponse.substring(lineStartPos, responseLength) + "#" + responseDetail + "#");
      if (incompleteResponse)
        lineStartPos += responseLength;
      else {
        strncpy(lastReceivedResponse, serialResponse, lineStartPos+responseLength);
        lastReceivedResponse[lineStartPos+responseLength+1] = 0;
        lineStartPos = 0;
        memset(serialResponse, 0, SERIAL_RESPONSE_SZ);
      }
      restartSerialTimeout();
      break; // Break out so printer detection can continue
    }
  }
#else
  /*
  // See handle_usb_rx for response handling with USB host mode
  while (PrinterSerial.available()) {
    char ch = (char)PrinterSerial.read();
    if (ch != '\n') {
      serialResponse += ch;
    } else {
      parseResponse();
    }
  }*/
#endif //USB_HOST_SERIAL

  if (commandQueue.isEmpty()) {
    restartSerialTimeout();
  } else if (!commandQueue.isAckEmpty() && (signed)(serialReceiveTimeoutTimer - millis()) <= 0) {
    log_w("Command '%s' has been lost by printer, rx buffer has been freed", commandQueue.peekSend());
    if (printerConnected)
      telnetSend("#TIMEOUT#");
    else
      commandQueue.clear();
    lineStartPos = 0;
    memset(serialResponse, 0, SERIAL_RESPONSE_SZ);
    restartSerialTimeout();
  }
  // this resets all the neopixels to an off state
  /* TODO neopixel
  strip.Begin();
  strip.Show();
  // strip.SetPixelColor(0, red);
  // strip.SetPixelColor(1, green);
  // strip.SetPixelColor(2, blue);
  // strip.SetPixelColor(3, white);
  int a;
  for(a=0; a<PixelCount; a++){
    strip.SetPixelColor(a, white);
  }
  strip.Show(); 
  */
}

void loop() {

  #ifdef USB_HOST_SERIAL
  //log_d("loop vcp");
  USBHost::loop();
  #endif // USB_HOST_SERIAL

  #ifdef OTA_UPDATES
  //log_d("check for OTA update");
    //****************
    //* OTA handling *
    //****************
    if (ESPrestartRequired) {  // check the flag here to determine if a restart is required
      PrinterSerial.printf("Restarting ESP\n\r");
      ESPrestartRequired = false;
      ESP.restart();
    }

    ArduinoOTA.handle();
  #endif

  //********************
  //* Printer handling *
  //********************
  if (!printerConnected) {
    if (USBHost::is_connected()) { // TODO wrap in preprocessor define
      printerConnected = detectPrinter();
    }
  }
  else {
    #ifndef OTA_UPDATES
     // MDNS.update();    // When OTA is active it's called by 'handle' method
    #endif

    //log_d("handle print");
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

    /* TODO reenable 
    if (!autoreportTempEnabled) {
      unsigned long curMillis = millis();
      if ((signed)(temperatureTimer - curMillis) <= 0) {
        commandQueue.push(TEMP_COMMAND);
        temperatureTimer = curMillis + TEMPERATURE_REPORT_INTERVAL * 1000;
      }
    }
    */
  }


  //log_d("send commands");
  SendCommands();
  //log_d("recv responses");
  ReceiveResponses();

  //*******************
  //* Telnet handling *
  //*******************

  //log_d("check telnet");
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
    
  #ifdef OTA_UPDATES
  //log_d("loop ota");
    AsyncElegantOTA.loop();
  #endif
}
