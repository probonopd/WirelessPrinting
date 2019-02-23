#if defined(ESP8266)
#include <ESP8266WiFi.h>
#include <ESP8266mDNS.h>
#include <ESPAsyncTCP.h>
#include <Ticker.h>
#endif

#if defined(ESP32)
#include <WiFi.h>
#include <ESPmDNS.h>
#include <AsyncTCP.h>
#include <ESP32Ticker.h>
#endif

#include <ArduinoOTA.h>

#define FS_NO_GLOBALS //allow spiffs to coexist with SD card, define BEFORE including FS.h

#include <SPI.h>
#include <SD.h>

#include <FS.h>
#include <SPIFFSEditor.h>

#include <DNSServer.h>

#include <ESPAsyncWebServer.h>
#include <ESPAsyncWiFiManager.h> // https://github.com/alanswx/ESPAsyncWiFiManager/

WiFiServer telnetServer(23);
WiFiClient serverClient;

AsyncWebServer server(80);
DNSServer dns;

// For implementing (a subset of) the OctoPrint API
#include "AsyncJson.h"
#include "ArduinoJson.h"

const char* sketch_version = "1.0";

const int DEFAULT_BAUD = 115200;  // Set your printer's baud

bool useFastSD=true; // Use Default fast SD clock in SD.begin(), set to false if your SD is an old or slow one.

bool okFound = true; // Set to true if last response from 3D printer was "ok", otherwise false
String response; // The last response from 3D printer

bool isPrinting = false;
bool shouldPrint = false;
bool shouldCancelPrint = false;
long lineNumberLastPrinted = 0;
String lineLastSent = "";
String lineLastReceived = "";
String lineSecondLastReceived = "";
bool hasSD = false; // will be set true if SD card is detected and usable; otherwise use SPIFFS

String priorityLine = ""; // A line that should be sent to the printer "in between"/before any other lines being sent. TODO: Extend to an array of lines
String commandLine = "";

String fwM115 = "Unknown"; // Result of M115
String device_name = "Unknown"; // Will be parsed from M115

Ticker statusTimer;
int statusInterval(2); // Ask the printer for its status every 2 seconds
bool shouldAskPrinterForStatus = false;

String filename = "cache.gco";
String filename_with_slash = filename; // Will be changed in setup()

String upload_name = "Unknown";
size_t filesize = 0;
size_t filesize_read = 0;

unsigned long millis_start;

Ticker blinker;

// Variables for printer status reporting
String temperature_actual = "0.0";
String temperature_target = "0.0";
String temperature_tool0_actual = "0.0";
String temperature_tool0_target = "0.0";
String temperature_bed_actual = "0.0";
String temperature_bed_target = "0.0";

// https://forum.arduino.cc/index.php?topic=228884.msg2670971#msg2670971
String IpAddress2String(const IPAddress& ipAddress)
{
  return String(ipAddress[0]) + String(".") + \
         String(ipAddress[1]) + String(".") + \
         String(ipAddress[2]) + String(".") + \
         String(ipAddress[3])  ;
}

void flip()
{
  int state = digitalRead(LED_BUILTIN);  // get the current state of GPIO1 pin
  digitalWrite(LED_BUILTIN, !state);     // set pin to the opposite state
}

void startBlinking(float secs) {
  blinker.attach(secs, flip);
}

void stopBlinking() {
  digitalWrite(LED_BUILTIN, HIGH);
  blinker.detach();
}

// Parse temperatures from printer responses like
// ok T:32.8 /0.0 B:31.8 /0.0 T0:32.8 /0.0 @:0 B@:0
String parseTemp(String response, String whichTemp, bool getTarget = false) {
  String temperature;
  int Tpos = response.indexOf(whichTemp + ":");
  if (Tpos > -1) { // This response contains a temperature
    int slashpos = response.indexOf(" /", Tpos);
    int spacepos = response.indexOf(" ", slashpos + 1);
    //if match mask T:xxx.xx /xxx.xx
    if (spacepos - Tpos < 17) {
      if (! getTarget) {
        temperature = response.substring(Tpos + whichTemp.length() + 1, slashpos);
      } else {
        temperature = response.substring(slashpos + 2, spacepos);
      }
    }
  }
  return (temperature);
}

void parseTemperatures(String response) {
  if (parseTemp(response, "T") != "") temperature_actual = parseTemp(response, "T");
  if (parseTemp(response, "T", true) != "") temperature_target = parseTemp(response, "T", true);
  if (parseTemp(response, "T0") != "") temperature_tool0_actual = parseTemp(response, "T0");
  if (parseTemp(response, "T0", true) != "") temperature_tool0_target = parseTemp(response, "T0", true);
  if (parseTemp(response, "B") != "") temperature_bed_actual = parseTemp(response, "B");
  if (parseTemp(response, "B", true) != "") temperature_bed_target = parseTemp(response, "B", true);
}

String sendToPrinter(String line) {

  /* Although this function does not return before okFound is true,
     somewhere else in the sketch this function might also have been called
     hence we make sure we have okFound before we start sending. */

  while (okFound == false) {
    yield();
  }

  Serial.setTimeout(240000); // How long we wait for "ok" in milliseconds

  if (shouldCancelPrint == true) {
    // Apparently we need to decide how to handle this
    // For now using M112 - Emergency Stop
    // http://marlinfw.org/docs/gcode/M112.html
        if (serverClient && serverClient.connected()) {  // send data to telnet client if connected
      serverClient.println("Should cancel print! This is not working yet");
    }
    Serial.println("M112"); // Send to 3D Printer immediately w/o waiting for anything
    Serial.println("M112"); // Send to 3D Printer immediately w/o waiting for anything
    Serial.println("M112"); // Send to 3D Printer immediately w/o waiting for anything
    Serial.println("M300 S500 P50"); // M300 - Play beep sound; Send to 3D Printer immediately w/o waiting for anything
    // sendToPrinter("M112");
    // sendToPrinter("M300 S500 P50"); // M300 - Play beep sound

    // ESP.restart(); // Maybe a bit too drastic?
    return "";
  }

  /* If a priority line exists, then send it before anything else.
     TODO: Extend this to handle multiple priority lines. */
  if (priorityLine != "") {
    String originalLine = line;
    line = priorityLine;
    priorityLine = "";
    sendToPrinter(line);
    sendToPrinter(originalLine);
  }

  Serial.println(line); // Send to 3D Printer

  if (serverClient && serverClient.connected()) {  // send data to telnet client if connected
    serverClient.println("> " + line);
  }

  lineLastSent = line;
  okFound = false;
  while (okFound == false) {
    response = Serial.readStringUntil('\n');
    if (serverClient && serverClient.connected()) {  // send data to telnet client if connected
      serverClient.println("< " + response);
      serverClient.print(millis());
      serverClient.print(", free heap RAM: ");
      serverClient.println(ESP.getFreeHeap());
      serverClient.println();
    }
    parseTemperatures(response);
    lineSecondLastReceived = lineLastReceived;
    lineLastReceived = response;
    if (response.startsWith("ok")) okFound = true;
  }
  return (response);
}

void lcd(String string) {
  sendToPrinter("M117 " + string);
}

void askPrinterForStatus() {
  if (isPrinting == false) {
    // Doing the following here takes too long and can crash:
    // sendToPrinter("M105");
    // Hence we just indicate that this should be done via setting a variable
    // and do the actual work from loop()
    shouldAskPrinterForStatus = true;
  }
}

void handlePrint() {

  // Do nothing if we are already printing. TODO: Give clear response
  if (isPrinting) {
    return;
  }

  sendToPrinter("M300 S500 P50"); // M300 - Play beep sound
  lcd("Printing...");
  millis_start = millis();
  shouldPrint = false;
  isPrinting = true;
  statusTimer.detach();

  int i = 0;
  filesize_read = 0;
  String line;

  if (!hasSD) {
    fs::File gcodeFile = SPIFFS.open("/" + filename, "r");

    if (gcodeFile) {
      while (gcodeFile.available()) {
        if(shouldCancelPrint == true){
          shouldCancelPrint == false;
          isPrinting = false;
          return;
        }
        lineNumberLastPrinted = lineNumberLastPrinted + 1;
        line = gcodeFile.readStringUntil('\n'); // The G-Code line being worked on
        filesize_read = filesize_read + line.length();
        int pos = line.indexOf(';');
        if (pos != -1) {
          line = line.substring(0, pos);
        }
        if ((line.startsWith("(")) || line.startsWith(";") || line.length() == 0 || line.startsWith("\r")) {
          continue;
        }
        sendToPrinter(line);

      }
    } else {
      lcd("Cannot open file");
    }
    isPrinting = false;
    statusTimer.attach(statusInterval, askPrinterForStatus);
    lcd("Complete");
    gcodeFile.close();
  } else {
    File gcodeFile = SD.open(filename, FILE_READ);

    if (gcodeFile) {
      while (gcodeFile.available()) {
        if(shouldCancelPrint == true){
          shouldCancelPrint == false;
          isPrinting = false;
          return;
        }
        lineNumberLastPrinted = lineNumberLastPrinted + 1;
        line = gcodeFile.readStringUntil('\n'); // The G-Code line being worked on
        filesize_read = filesize_read + line.length();
        int pos = line.indexOf(';');
        if (pos != -1) {
          line = line.substring(0, pos);
        }
        if ((line.startsWith("(")) || line.startsWith(";") || line.length() == 0 || line.startsWith("\r")) {
          continue;
        }
        sendToPrinter(line);

      }
    } else {
      lcd("Cannot open file");
    }
    isPrinting = false;
    statusTimer.attach(statusInterval, askPrinterForStatus);
    lcd("Complete");
    gcodeFile.close();

  }

}

fs::File f; // SPIFFS
File uploadFile; // SD card

void handleUpload(AsyncWebServerRequest * request, String filename, size_t index, uint8_t *data, size_t len, bool final) {
  filesize = 0; // Will set the correct one below
  upload_name = filename;

  if (!hasSD) { // No SD, hence use SPIFFS



    if (!index) {
      f = SPIFFS.open(filename_with_slash, "w"); // create or truncate file
    }

    if (len) { // uploading
      f = SPIFFS.open(filename_with_slash, "a"); // append to file (for chunked upload) //////////// REALLY NEEDED???
      ESP.wdtDisable();
      f.write(data, len);
      ESP.wdtEnable(10);
    }

    if (final) { // upload finished
      f.close();
      filesize = index + len;
      shouldPrint = true;
    }

  } else { // has SD, hence use it

    if (!index) {
      if (SD.exists((char *)filename_with_slash.c_str())) SD.remove((char *)filename_with_slash.c_str());
      uploadFile = SD.open(filename_with_slash.c_str(), FILE_WRITE);
    }

    if (len) { // uploading
      for (size_t i = 0; i < len; i++) {
        uploadFile.write(data[i]);
      }
    }

    if (final) { // upload finished
      uploadFile.close();
      filesize = index + len;
      shouldPrint = true;
    }
  }
}

void setup() {
  delay(3000);


  if (!filename.startsWith("/")) filename_with_slash = "/" + filename;

  if(useFastSD){
    if (SD.begin(SS, 50000000)) { // https://github.com/esp8266/Arduino/issues/1853
    hasSD = true;
    }
  } else {
    if (SD.begin()) { 
      hasSD = true;
    }
  }

  Serial.begin(DEFAULT_BAUD);

  // Wait until we detect a printer - seemingly not needed? Using this would have the disadvantage that you always need to reset the printer as well as the ESP
  // Serial.flush(); //flush all previous received and transmitted data
  // while(!Serial.available()) ; // hang program until a byte is received notice the ; after the while()

  String text;

  // Wait for connection
  pinMode(LED_BUILTIN, OUTPUT);     // Initialize the LED_BUILTIN pin as an output
  digitalWrite(LED_BUILTIN, LOW);   // Turn the LED on (Note that LOW is the voltage level
  AsyncWiFiManager wifiManager(&server, &dns);
  // wifiManager.resetSettings();   // Uncomment this to reset the settings on the device, then you will need to reflash with USB and this commented out!
  wifiManager.setDebugOutput(false); // So that it does not send stuff to the printer that the printer does not understand
  wifiManager.autoConnect("AutoConnectAP");
  digitalWrite(LED_BUILTIN, HIGH);  // Turn the LED off (Note that LOW is the voltage level

  telnetServer.begin();
  telnetServer.setNoDelay(true);

  text = IpAddress2String(WiFi.localIP());
  if (hasSD) {
    text += " SD";
  } else {
    text += " SPIFFS";
  }
  lcd(text);
  sendToPrinter("M300 S500 P50"); // M300 - Play beep sound

  sendToPrinter("M115"); // M115 - Firmware Info

  // Parse the name of the machine from M115
  fwM115 = lineSecondLastReceived;
  String fwMACHINE_TYPE = "Unknown";
  if ((lineSecondLastReceived.indexOf("MACHINE_TYPE:") > -1) && (lineSecondLastReceived.indexOf("EXTRUDER_COUNT:") > -1)) {
    fwMACHINE_TYPE = lineSecondLastReceived.substring(lineSecondLastReceived.indexOf("MACHINE_TYPE:") + 13, lineSecondLastReceived.indexOf("EXTRUDER_COUNT:") - 1);
  }

  char output[9];
  itoa(ESP.getChipId(), output, 16);
  String chip_id = String(output);
  device_name = fwMACHINE_TYPE + " (" + chip_id + ")";

  if (MDNS.begin(device_name.c_str())) {

    // For Cura WirelessPrint - deprecated in favor of the OctoPrint API
    MDNS.addService("wirelessprint", "tcp", 80);
    MDNS.addServiceTxt("wirelessprint", "tcp", "version", sketch_version);

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

  // ArduinoOTA.setHostname(host);
  ArduinoOTA.begin();

  MDNS.addService("http", "tcp", 80);

  if (!hasSD) {
    SPIFFS.begin();
    server.addHandler(new SPIFFSEditor());
  }

  server.on("/heap", HTTP_GET, [](AsyncWebServerRequest * request) {
    request->send(200, "text/plain", String(ESP.getFreeHeap()));
  });

  server.on("/", HTTP_GET, [](AsyncWebServerRequest * request) {
    String message = "<h1>" + device_name + "</h1>";
    message += "<form enctype=\"multipart/form-data\" action=\"/api/files/local\" method=\"POST\">\n";
    message += "<p>You can also print from the command line using curl:</p>\n";
    message += "<pre>curl -F \"file=@/path/to/some.gcode\" ";
    message += IpAddress2String(WiFi.localIP());
    message += "/api/files/local</pre>\n";
    message += "<input type=\"hidden\" name=\"MAX_FILE_SIZE\" value=\"100000\" />\n";
    message += "Choose a file to upload: <input name=\"file\" type=\"file\" /><br />\n";
    message += "<input type=\"submit\" value=\"Upload\" />\n";
    message += "</form>";
    message += "";
    message += "<p><a href=\"/download\">Download</a></p>";
    message +=  String("<pre>") +
                String("fwM115: ") + fwM115 + String("\n") +
                String("filesize_read: ") + String(filesize_read) + String("\n") +
                String("lineLastSent: ") + lineLastSent + String("\n") +
                String("lineSecondLastReceived: ") + lineSecondLastReceived + String("\n") +
                String("lineLastReceived: ") + lineLastReceived + String("\n") + String("</pre>");
    request->send(200, "text/html", message);
  });

  // For Slic3r OctoPrint compatibility

  server.on("/api/files/local", HTTP_POST, [](AsyncWebServerRequest * request) {
    // sendToPrinter("M300 S500 P50"); // M300 - Play beep sound - THIS LEADS TO CRASHES DURING UPLOAD!
    // lcd("Receiving..."); - THIS LEADS TO CRASHES DURING UPLOAD!
    // lcd(request->contentType()); - THIS LEADS TO CRASHES DURING UPLOAD!
    // http://docs.octoprint.org/en/master/api/files.html#upload-response

    if (!request->hasParam("file", true, true)) {
      lcd("Needs PR #192"); // Cura
      // Cura needs https://github.com/me-no-dev/ESPAsyncWebServer/pull/192
    }

    request->send(200, "application/json", "{\r\n  \"files\": {\r\n    \"local\": {\r\n      \"name\": \"" + filename + "\",\r\n      \"size\": \"" + String(filesize) + "\",\r\n      \"origin\": \"local\",\r\n      \"refs\": {\r\n        \"resource\": \"\",\r\n        \"download\": \"\"\r\n      }\r\n    }\r\n  },\r\n  \"done\": true\r\n}\r\n");
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
    float percentage = 0.0;
    if ( filesize_read > 0 ) {
      percentage = ((float)filesize_read / (float)filesize) * 100; // Not super accurate but what OctoPrint does, too
    }
    int seconds_since_start = 0;
    int seconds_remaining = 0;
    if (isPrinting == true) {
      seconds_since_start = (millis() - millis_start) / 1000;
      seconds_remaining = seconds_since_start / percentage * (100.0 - percentage);
    }
    request->send(200, "application/json", "{\r\n  \"job\": {\r\n    \"file\": {\r\n      \"name\": \"" + upload_name + "\",\r\n      \"origin\": \"local\",\r\n      \"size\": " + String(filesize) + ",\r\n      \"date\": 1378847754\r\n    },\r\n    \"estimatedPrintTime\": 8811,\r\n    \"filament\": {\r\n      \"length\": 810,\r\n      \"volume\": 5.36\r\n    }\r\n  },\r\n  \"progress\": {\r\n    \"completion\": " + String(percentage) + ",\r\n    \"filepos\": " + String(filesize_read) + ",\r\n    \"printTime\": " + String(seconds_since_start) + ",\r\n    \"printTimeLeft\": " + String(seconds_remaining) + "\r\n  }\r\n}");
  });
  // TODO: Implement POST. Cura uses this to pause and abort prints.
  
  server.on("/api/settings", HTTP_GET, [](AsyncWebServerRequest * request) {
    // https://github.com/probonopd/WirelessPrinting/issues/30
    request->send(200, "application/json", "{\r\n}");
  });

  server.on("/api/printer", HTTP_GET, [](AsyncWebServerRequest * request) {
    // http://docs.octoprint.org/en/master/api/datamodel.html#printer-state
    // NOTE: Currently reporting T instead of T0 because not all machines report T0; see https://github.com/probonopd/WirelessPrinting/issues/4#issuecomment-321975846
    request->send(200, "application/json", "{\r\n  \"temperature\": {\r\n    \"tool0\": {\r\n      \"actual\": " + temperature_actual + ",\r\n      \"target\": " + temperature_target + ",\r\n      \"offset\": 0\r\n    },\r\n    \"bed\": {\r\n      \"actual\": " + temperature_bed_actual + ",\r\n      \"target\": " + temperature_bed_target + ",\r\n      \"offset\": 0\r\n    }\r\n  },\r\n  \"sd\": {\r\n    \"ready\": " + String(hasSD) + "\r\n  },\r\n  \"state\": {\r\n    \"text\": \"Operational\",\r\n    \"flags\": {\r\n      \"operational\": true,\r\n      \"paused\": false,\r\n      \"printing\": " + String(isPrinting) + ",\r\n      \"sdReady\": " + String(hasSD) + ",\r\n      \"error\": false,\r\n      \"ready\": true,\r\n      \"closedOrError\": false\r\n    }\r\n  }\r\n}");
    // request->send(200, "application/json", "{\r\n  \"temperature\": {\r\n    \"tool0\": {\r\n      \"actual\": 0.0,\r\n      \"target\": 0.0,\r\n      \"offset\": 0\r\n    },\r\n    \"bed\": {\r\n      \"actual\": 0.0,\r\n      \"target\": 0.0,\r\n      \"offset\": 0\r\n    }\r\n  },\r\n  \"sd\": {\r\n    \"ready\": true\r\n  },\r\n  \"state\": {\r\n    \"text\": \"Operational\",\r\n    \"flags\": {\r\n      \"operational\": true,\r\n      \"paused\": false,\r\n      \"printing\": " + String(isPrinting) + ",\r\n      \"sdReady\": true,\r\n      \"error\": false,\r\n      \"ready\": true,\r\n      \"closedOrError\": false\r\n    }\r\n  }\r\n}");
  });

  // Parse POST JSON data, https://github.com/me-no-dev/ESPAsyncWebServer/issues/195
  server.onRequestBody([](AsyncWebServerRequest * request, uint8_t *data, size_t len, size_t index, size_t total) {
    DynamicJsonBuffer jsonBuffer;
    if ((request->url() == "/api/printer/command") || (request->url() == "/api/job")) {

      JsonObject& root = jsonBuffer.parseObject((const char*)data);
      if (root.success()) {
        if (root.containsKey("command")) {

          String command = root["command"].asString();

          // Cura uses this to Pre-heat the build plate (M140)
          // http://docs.octoprint.org/en/master/api/printer.html#send-an-arbitrary-command-to-the-printer
          // sendToPrinter(root["command"]); // Crashes when done here. Takes too long for inside a callback?
          if (serverClient && serverClient.connected()) {  // send data to telnet client if connected
            root.prettyPrintTo(serverClient);
          }

          // Cura uses this to "Abort print"
          // http://docs.octoprint.org/en/master/api/job.html
          if (command == "cancel") {
            shouldCancelPrint = true;
          } else {
            commandLine = command;
            if (isPrinting == true) {
              priorityLine = commandLine;
            }
          }
        }
      }
      request->send(204);
    }
  });

  // For Cura 2.7.0 OctoPrintPlugin compatibility
  // https://github.com/probonopd/WirelessPrinting/issues/18#issuecomment-321927016
  server.on("/api/settings", HTTP_GET, [](AsyncWebServerRequest * request) {
    request->send(200, "application/json", "{}");
  });

  // For legacy PrusaControlWireless - deprecated in favor of the OctoPrint API
  server.on("/print", HTTP_POST, [](AsyncWebServerRequest * request) {
    request->send(200, "text/plain", "Received");
  }, handleUpload);

  // For legacy Cura WirelessPrint - deprecated in favor of the OctoPrint API
  server.on("/api/print", HTTP_POST, [](AsyncWebServerRequest * request) {
    request->send(200, "text/plain", "Received");
  }, handleUpload);

  server.onNotFound([](AsyncWebServerRequest * request) {
    request->send(404);
    if (serverClient && serverClient.connected()) {  // send data to telnet client if connected
      serverClient.println("404");
      serverClient.println(request->url());
      serverClient.println("");
    }

  });

  server.begin();

  /* Set up the timer to fire every 2 seconds */
  statusTimer.attach(statusInterval, askPrinterForStatus);

}

void loop() {
  ArduinoOTA.handle();
  if (shouldPrint == true) handlePrint();

  if (commandLine != "") {
    if (isPrinting == false) {
      sendToPrinter(commandLine);
      commandLine = "";
    } else {
      priorityLine = commandLine;
    }
  }

  if (shouldAskPrinterForStatus) {
    shouldAskPrinterForStatus = false;
    sendToPrinter("M105"); // Doing this in the ticker callback would take too long and crash it
  }

  // look for Client connect trial
  if (telnetServer.hasClient()) {
    if (!serverClient || !serverClient.connected()) {
      if (serverClient) {
        serverClient.stop();
      }
      serverClient = telnetServer.available();
      serverClient.flush();  // clear input buffer, else you get strange characters
    }
  }

  while (serverClient.available()) { // get data from Client
    Serial.write(serverClient.read());
  }

  // delay(10);  // to avoid strange characters left in buffer

}
