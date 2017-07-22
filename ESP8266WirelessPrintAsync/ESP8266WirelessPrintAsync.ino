/*
   Optionally uses SD shield; if not available uses SPIFFS (slower and smaller)
   TODO
   Remove hardcoded cache.gco
*/


#include <ESP8266WiFi.h>
#include <ESP8266mDNS.h>
#include <ArduinoOTA.h>

#define FS_NO_GLOBALS //allow spiffs to coexist with SD card, define BEFORE including FS.h

#include <SPI.h>
#include <SD.h>

#include <FS.h>
#include <SPIFFSEditor.h>

#include <DNSServer.h>

#include <ESPAsyncTCP.h>
#include <ESPAsyncWebServer.h>
#include <ESPAsyncWiFiManager.h> // https://github.com/alanswx/ESPAsyncWiFiManager/

AsyncWebServer server(80);
DNSServer dns;

const char * host = "WirelessPrintingAsync";

const char* sketch_version = "1.0";

/* Access SDK functions for timer */
extern "C" {
#include "user_interface.h"
}
os_timer_t myTimer;

bool okFound = true; // Set to true if last response from 3D printer was "ok", otherwise false
String response; // The last response from 3D printer

bool isPrinting = false;
bool shouldPrint = false;
long lineNumberLastPrinted = 0;
String lineLastSent = "";
String lineLastReceived = "";
bool hasSD = false; // will be set true if SD card is detected and usable; otherwise use SPIFFS

String priorityLine = ""; // A line that should be sent to the printer "in between"/before any other lines being sent. TODO: Extend to an array of lines

const int timerInterval = 2000; // Tick every 2 seconds
bool tickOccured = false; // Whether the timer has fired

void timerCallback(void *pArg) {
  tickOccured = true;
}

// https://forum.arduino.cc/index.php?topic=228884.msg2670971#msg2670971
String IpAddress2String(const IPAddress& ipAddress)
{
  return String(ipAddress[0]) + String(".") + \
         String(ipAddress[1]) + String(".") + \
         String(ipAddress[2]) + String(".") + \
         String(ipAddress[3])  ;
}

String sendToPrinter(String line) {

  /* Although this function does not return before okFound is true,
     somewhere else in the sketch this function might also have been called
     hence we make sure we have okFound before we start sending. */

  while (okFound == false) {
    yield();
  }

  Serial.setTimeout(240000); // How long we wait for "ok" in milliseconds

  /* If a priority line exists (e.g., a stop command), then send it before anything else.
     TODO: Extend this to handle multiple priority lines. */
  if (priorityLine != "") {
    String originalLine = line;
    line = priorityLine;
    priorityLine = "";
    sendToPrinter(line);
    sendToPrinter(originalLine);
  }

  Serial.println(line); // Send to 3D Printer

  lineLastSent = line;
  okFound = false;
  while (okFound == false) {
    response = Serial.readStringUntil('\n');
    lineLastReceived = response;
    if (response.startsWith("ok")) okFound = true;
  }
  return (response);
}

void lcd(String string) {
  sendToPrinter("M117 " + string);
}

void handlePrint() {

  // Do nothing if we are already printing. TODO: Give clear response
  if (isPrinting) {
    return;
  }

  sendToPrinter("M300 S500 P50"); // M300: Play beep sound
  lcd("Printing...");

  shouldPrint = false;
  isPrinting = true;
  os_timer_disarm(&myTimer);

  int i = 0;
  String line;

  if (!hasSD) {
    fs::File gcodeFile = SPIFFS.open("/cache.gco", "r");

    if (gcodeFile) {
      while (gcodeFile.available()) {
        lineNumberLastPrinted = lineNumberLastPrinted + 1;
        line = gcodeFile.readStringUntil('\n'); // The G-Code line being worked on
        int pos = line.indexOf(';');
        if (pos != -1) {
          line = line.substring(0, pos);
        }
        if ((line.startsWith("(")) || (line.startsWith(";")) || (line.length() == 0)) {
          continue;
        }
        sendToPrinter(line);

      }
    } else {
      lcd("Cannot open file");
    }
    isPrinting = false;
    os_timer_arm(&myTimer, timerInterval, true);
    lcd("Complete");
    gcodeFile.close();
  } else {
    File gcodeFile = SD.open("cache.gco", FILE_READ);

    if (gcodeFile) {
      while (gcodeFile.available()) {
        lineNumberLastPrinted = lineNumberLastPrinted + 1;
        line = gcodeFile.readStringUntil('\n'); // The G-Code line being worked on
        int pos = line.indexOf(';');
        if (pos != -1) {
          line = line.substring(0, pos);
        }
        if ((line.startsWith("(")) || (line.startsWith(";")) || (line.length() == 0)) {
          continue;
        }
        sendToPrinter(line);

      }
    } else {
      lcd("Cannot open file");
    }
    isPrinting = false;
    os_timer_arm(&myTimer, timerInterval, true);
    lcd("Complete");
    gcodeFile.close();

  }

}

void setup() {


  if (SD.begin(SS, 50000000)) { // https://github.com/esp8266/Arduino/issues/1853
    hasSD = true;
  }

  delay(5000); // 3D printer needs this time
  Serial.begin(115200);

  String text;

  // Wait for connection
  pinMode(LED_BUILTIN, OUTPUT);     // Initialize the LED_BUILTIN pin as an output
  digitalWrite(LED_BUILTIN, LOW);   // Turn the LED on (Note that LOW is the voltage level
  AsyncWiFiManager wifiManager(&server, &dns);
  // wifiManager.resetSettings();   // Uncomment this to reset the settings on the device, then you will need to reflash with USB and this commented out!
  wifiManager.autoConnect("AutoConnectAP");
  digitalWrite(LED_BUILTIN, HIGH);  // Turn the LED off (Note that LOW is the voltage level
  text = IpAddress2String(WiFi.localIP());
  if (hasSD) {
    text += " SD";
  }
  lcd(text);
  sendToPrinter("M300 S500 P50"); // M300: Play beep sound

  if (MDNS.begin(host)) {

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

  ArduinoOTA.setHostname(host);
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
    String message = "<h1>WirelessPrint</h1>";
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
    message +=  String("<pre>") + String("lineLastSent: ") + lineLastSent + String("\n") +
                String("lineLastReceived: ") + lineLastReceived + String("\n") + String("</pre>");
    request->send(200, "text/html", message);
  });



  // For Slic3r OctoPrint compatibility
  server.on("/api/files/local", HTTP_POST, [](AsyncWebServerRequest * request) {
    request->send(200, "text/plain", "Received");
  }, handleUpload);
  server.on("/api/version", HTTP_GET, [](AsyncWebServerRequest * request) {
    request->send(200, "text/plain", sketch_version);
  });

  // For PrusaControlWireless - deprecated in favor of the OctoPrint API
  server.on("/print", HTTP_POST, [](AsyncWebServerRequest * request) {
    request->send(200, "text/plain", "Received");
  }, handleUpload);

  // For Cura WirelessPrint - deprecated in favor of the OctoPrint API
  server.on("/api/print", HTTP_POST, [](AsyncWebServerRequest * request) {
    request->send(200, "text/plain", "Received");
  }, handleUpload);


  server.onNotFound([](AsyncWebServerRequest * request) {
    request->send(404);
  });

  server.begin();

  /* Set up the timer to fire every 2 seconds */
  os_timer_setfn(&myTimer, timerCallback, NULL);
  os_timer_arm(&myTimer, timerInterval, true);

}

/*
  void handleUpload(AsyncWebServerRequest *request, String filename, size_t index, uint8_t *data, size_t len, bool final) {
  //sendToPrinter("M300 S500 P50"); // M300: Play beep sound
  // digitalWrite(LED_BUILTIN, LOW);   // Turn the LED on
  lcd("Receiving...");
  File f;
  filename = "cache.gco" ; // Override whatever the client had sent
  if (!filename.startsWith("/")) filename = "/" + filename;
  if (!index) {
    if (SPIFFS.exists(filename)) {
      SPIFFS.remove(filename);
      // TODO: check if file fits on filesystem; respond with error code if not
    }
    f = SPIFFS.open(filename, "w"); // create or trunicate file
  }
  else f = SPIFFS.open(filename, "a"); // append to file (for chunked upload)
  f.write(data, len);
  if (final) { // upload finished
    f.close();
    lcd("Received");
    // sendToPrinter("M300 S500 P50"); // M300: Play beep sound
    // digitalWrite(LED_BUILTIN, HIGH);  // Turn the LED off
    shouldPrint = true;
  }
  }

  // This works!!!
  File f;
  void handleUpload(AsyncWebServerRequest *request, String filename, size_t index, uint8_t *data, size_t len, bool final){
  filename = "/cache.gco";
  if(!filename.startsWith("/")) filename = "/" + filename;

  if(!index){
    f = SPIFFS.open(filename, "w"); // create or trunicate file
  }

  if(len){ // uploading
    f = SPIFFS.open(filename, "a"); // append to file (for chunked upload)
    ESP.wdtDisable();
    // lcd(String(len)); // Crashes it?!
    f.write(data, len);
    ESP.wdtEnable(10);
  }

  if(final){ // upload finished
    f.close();
    shouldPrint = true;
  }
  }

*/


fs::File f; // SPIFFS
File uploadFile; // SD card

void handleUpload(AsyncWebServerRequest * request, String filename, size_t index, uint8_t *data, size_t len, bool final) {
  if (!hasSD) {
  

    filename = "/cache.gco";
    if (!filename.startsWith("/")) filename = "/" + filename;

    if (!index) {
      f = SPIFFS.open(filename, "w"); // create or truncate file
    }

    if (len) { // uploading
      f = SPIFFS.open(filename, "a"); // append to file (for chunked upload) //////////// REALLY NEEDED???
      ESP.wdtDisable();
      // lcd(String(len)); // Crashes it?!
      f.write(data, len);
      ESP.wdtEnable(10);
    }

    if (final) { // upload finished
      f.close();
      shouldPrint = true;
    }


  } else {

    filename = "cache.gco";

    if (!index) {
      if (SD.exists((char *)filename.c_str())) SD.remove((char *)filename.c_str());
      uploadFile = SD.open(filename.c_str(), FILE_WRITE);
    }

    if (len) { // uploading
      for (size_t i = 0; i < len; i++) {
        uploadFile.write(data[i]);
      }
    }

    if (final) { // upload finished
      uploadFile.close();
      shouldPrint = true;
    }
  }
}

void loop() {
  ArduinoOTA.handle();
  if (shouldPrint == true) handlePrint();

  /* When the timer has ticked and we are not printing, ask for temperature */
  //  if ((isPrinting == false) && (tickOccured == true)) {
  //    sendToPrinter("M105");
  //    tickOccured = false;
  //  }
}
