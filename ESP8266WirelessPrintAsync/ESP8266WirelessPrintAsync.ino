/*
  Works with Arduino Hourly and ESP git as of March 4, 2017
  when using a 2 GB card formatted with the SD Association's formatter
  Send G-Code stored on SD card
  This sketch reads G-Code from a file from the SD card using the
  SD library and sends it over the serial port.
  Basic G-Code sending:
  http://fightpc.blogspot.de/2016/08/g-code-over-wifi.html
  Advanced G-Code sending:
  https://github.com/Ultimaker/Cura/blob/master/plugins/USBPrinting/USBPrinterOutputDevice.py
  The circuit:
  SD card attached to WeMos D1 mini as follows:
  MOSI - pin D7
  MISO - pin D6
  CLK - pin D5
  CS - pin D8
  capacitor across power pins of SD card for getting the card recognized
*/

/*
 * Need to apply https://github.com/esp8266/Arduino/pull/3079/files
 * in order for SDFile to work
 */

#define FS_NO_GLOBALS

#include <ESP8266WiFi.h>
#include <ESP8266mDNS.h>
#include <WiFiUdp.h>
#include <ArduinoOTA.h>
#include <ESPAsyncWebServer.h>
#include <SPI.h>
#include <SD.h>
// !!! Make sure it says
// Multiple libraries were found for "SD.h"
// Used: (...)/hardware/esp8266/esp8266/libraries/SD <-- MUST use this one
// Not used: (...)/libraries/SD <-- Must NOT use this one

#include "private.h"
// const char* ssid = "____";
// const char* password = "____";

/* Access SDK functions for timer */
extern "C" {
#include "user_interface.h"
}

os_timer_t myTimer;

// #define MTU_Size 2*1460 // https://github.com/esp8266/Arduino/issues/1853

const char* sketch_version = "1.0";

const char* host = "3d";
const int chipSelect = SS;

const String uploadfilename = "cache.gco"; // 8+3 limitation of FAT, otherwise won't be written

bool okFound = true; // Set to true if last response from 3D printer was "ok", otherwise false
String response; // The last response from 3D printer

bool isPrinting = false;
bool shouldPrint = false;
long lineNumberLastPrinted = 0;
String lineLastSent = "";
String lineLastReceived = "";
String jobName = "";

String priorityLine = ""; // A line that should be sent to the printer "in between"/before any other lines being sent. TODO: Extend to an array of lines

const int timerInterval = 2000; // Tick every 2 seconds
bool tickOccured = false; // Whether the timer has fired

void timerCallback(void *pArg) {
  tickOccured = true;
}

bool SD_exists(String path) {
  bool exists = false;
  SDFile test = SD.open(path);
  if (test) {
    test.close();
    exists = true;
  }
  return exists;
}

class AsyncSDFileResponse: public AsyncAbstractResponse {
  private:
    SDFile _content;
    String _path;
    void _setContentType(String path) {
      if (path.endsWith(".gco")) _contentType = "text/plain";
      else _contentType = "application/octet-stream";
    }

  public:
    AsyncSDFileResponse(String path, String contentType = String(), bool download = false) {
      _code = 200;
      _path = uploadfilename;
      if (download)
        _contentType = "application/octet-stream";
      else
        _setContentType(path);
      _content = SD.open(_path);
      _contentLength = _content.size();
    }
    ~AsyncSDFileResponse() {
      if (_content)
        _content.close();
    }
    bool _sourceValid() {
      return !!(_content);
    }
    size_t _fillBuffer(uint8_t *buf, size_t maxLen) {
      int r = _content.read(buf, maxLen);
      if (r < 0) {
        os_printf("Error\n");
        _content.close();
        return 0;
      }
      return r;
    }
};

String sendToPrinter(String line) {

  /* Although this function does not return before okFound is true,
     somewhere else in the sketch this function might also have been called
     hence we make sure we have okFound before we start sending. */

  while (okFound == false) {
    yield();
  }

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
  // Serial.println(string);
  sendToPrinter("M117 " + string);
}

/* Start webserver functions */

AsyncWebServer server(80);

static bool hasSD = false;
SDFile uploadFile;

/* This function streams out the G-Code to the printer */

void handlePrint() {

  shouldPrint = false;
  isPrinting = true;
  os_timer_disarm(&myTimer);

  int i = 0;
  SDFile gcodeFile = SD.open(uploadfilename.c_str(), FILE_READ);
  String line;
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
    lcd("File is not on SD card");
  }
  isPrinting = false;
  jobName = "";
  os_timer_arm(&myTimer, timerInterval, true);
  lcd("Complete");
}

// https://forum.arduino.cc/index.php?topic=228884.msg2670971#msg2670971
String IpAddress2String(const IPAddress& ipAddress)
{
  return String(ipAddress[0]) + String(".") + \
         String(ipAddress[1]) + String(".") + \
         String(ipAddress[2]) + String(".") + \
         String(ipAddress[3])  ;
}

void setup() {
  delay(3000); // 3D printer needs this time
  Serial.begin(115200);
  Serial.setTimeout(240000); // How long we wait for "ok" in milliseconds
  WiFi.begin(ssid, password);
  String text = "Connecting to ";
  text = text + ssid;
  lcd(text);

  // Wait for connection
  uint8_t i = 0;
  while (WiFi.status() != WL_CONNECTED && i++ < 20) {//wait 10 seconds
    delay(500);
  }
  if (i == 21) {
    text = "Could not connect to ";
    text = text + ssid;
    lcd(text);
    while (1) delay(500);
  }
  lcd(IpAddress2String(WiFi.localIP()));

  if (MDNS.begin(host)) {
    MDNS.addService("http", "tcp", 80);
    MDNS.addService("wirelessprint", "tcp", 80);
    MDNS.addServiceTxt("wirelessprint", "tcp", "version", sketch_version);
  }

  text = "http://";
  text = text + host;
  text = text + ".local";

  // Hostname defaults to esp8266-[ChipID]
  ArduinoOTA.setHostname(host);

  ArduinoOTA.begin();

  server.on("/", HTTP_GET, [](AsyncWebServerRequest * request) {
    // TODO: Offer stop when print is running
    String message = "<h1>WirelessPrint</h1>";
    if (isPrinting == false) {
      message += "<form enctype=\"multipart/form-data\" action=\"/print\" method=\"POST\">\n";
      message += "<p>You can also print from the command line using curl:</p>\n";
      message += "<pre>curl -F \"file=@/path/to/some.gcode\" 3d.local/print</pre>\n";
      message += "<input type=\"hidden\" name=\"MAX_FILE_SIZE\" value=\"100000\" />\n";
      message += "Choose a file to upload: <input name=\"file\" type=\"file\" /><br />\n";
      message += "<input type=\"submit\" value=\"Upload\" />\n";
      message += "</form>";
      message += "";
      message += "<p><a href=\"/download\">Download</a></p>";
    }
    request->send(200, "text/html", message + "\r\n");
  });

/* Cura asks the printer for status periodically.
   We should make it easy on the ESP8266 and not do too much processing here
   because a print may be in progress at the same time.
   So avoid JSON processing, not expensive calculations, let Cura do them */

  server.on("/status", HTTP_GET, [](AsyncWebServerRequest * request) {
    String message = "[Status]\n";
    message += "lineLastSent=" + lineLastSent + "\n";
    message += "lineLastReceived=" + lineLastReceived + "\n";
    message += "isPrinting=" + String(isPrinting) + "\n";
    message += "lineNumberLastPrinted=" + String(lineNumberLastPrinted) + "\n";
    message += "jobName=" + jobName + "\n";
    request->send(200, "text/plain", message + "\r\n");
  });

  server.onFileUpload([](AsyncWebServerRequest * request, String filename, size_t index, uint8_t *data, size_t len, bool final) {
    if (!index) {
      lcd("Receiving...");
      if (SD_exists((char *)uploadfilename.c_str())) SD.remove((char *)uploadfilename.c_str());
      // jobName = request->getParam("path", true)->value(); ////////////////////////////////////////// CRASHES
      // delay(500);
      uploadFile = SD.open(uploadfilename, FILE_WRITE | O_TRUNC);
    }
    if (len && uploadFile)
      uploadFile.write(data, len);
    if (final) {
      if (uploadFile) uploadFile.close();
      delay(50);
      lcd("Received");
      delay(1000); // So that we can read the message
      shouldPrint = true; // This is seen by loop() and the printing is then started, but this function can exit, hence ending the HTTP transmission
    }
  });

  server.on("/download", HTTP_GET, [](AsyncWebServerRequest * request) {
      request->send(new AsyncSDFileResponse("/download"));
  });

  server.begin();

  if (SD.begin(SS, 50000000)) { // https://github.com/esp8266/Arduino/issues/1853
  hasSD = true;
  lcd("SD Card OK");
    delay(1000); // So that we can read the last message on the LCD
    lcd(text); // may be too large to fit on screen
  } else {
    lcd("SD Card ERROR");
  }

  /* Set up the timer to fire every 2 seconds */
  os_timer_setfn(&myTimer, timerCallback, NULL);
  os_timer_arm(&myTimer, timerInterval, true);

}

void loop() {
  ArduinoOTA.handle();
  if (shouldPrint == true) handlePrint();

  /* When the timer has ticked and we are not printing, ask for temperature */
  if ((isPrinting == false) && (tickOccured == true)) {
    sendToPrinter("M105");
    tickOccured = false;
  }

}
