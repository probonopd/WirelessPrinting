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

#include <ESP8266WiFi.h>
#include <ESP8266mDNS.h>
#include <WiFiUdp.h>
#include <ArduinoOTA.h>
#include <SPI.h>
#include <SD.h>
#include <ESP8266WebServer.h>
#include "IniFile.h"

/* Access SDK functions for timer */
extern "C" {
#include "user_interface.h"
}

os_timer_t myTimer;

// #define MTU_Size 2*1460 // https://github.com/esp8266/Arduino/issues/1853

const char* sketch_version = "1.0";

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
  sendToPrinter("M117 " + string);
}

/* Start webserver functions */

ESP8266WebServer server(80);

static bool hasSD = false;
File uploadFile;

void returnOK() {
  server.send(200, "text/plain", "");
}

void returnFail(String msg) {
  server.send(500, "text/plain", msg + "\r\n");
}

/* Cura asks the printer for status periodically.
   We should make it easy on the ESP8266 and not do too much processing here
   because a print may be in progress at the same time.
   So avoid JSON processing, not expensive calculations, let Cura do them */

void handleStatus() {
  String message = "[Status]\n";
  message += "lineLastSent=" + lineLastSent + "\n";
  message += "lineLastReceived=" + lineLastReceived + "\n";
  message += "isPrinting=" + String(isPrinting) + "\n";
  message += "lineNumberLastPrinted=" + String(lineNumberLastPrinted) + "\n";
  message += "jobName=" + jobName + "\n";

  server.send(200, "text/plain", message + "\r\n");
}

HTTPUpload& upload = server.upload();
void handleFileUpload() {
  if (upload.status == UPLOAD_FILE_START) {
    lcd("Receiving...");
    if (SD.exists((char *)uploadfilename.c_str())) SD.remove((char *)uploadfilename.c_str());
    jobName = upload.filename;
    delay(500);
    uploadFile = SD.open(uploadfilename.c_str(), FILE_WRITE);
  } else if (upload.status == UPLOAD_FILE_WRITE) {
    if (uploadFile) uploadFile.write(upload.buf, upload.currentSize);
  } else if (upload.status == UPLOAD_FILE_END) {
    if (uploadFile) uploadFile.close();
    delay(50);
    lcd("Received");
    delay(1000); // So that we can read the message
    shouldPrint = true; // This is seen by loop() and the printing is then started, but this function can exit, hence ending the HTTP transmission
  }
}

void handleIndex() {
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
  server.send(200, "text/html", message);
}

/* This function streams out the G-Code to the printer */

void handlePrint() {

  shouldPrint = false;
  isPrinting = true;
  os_timer_disarm(&myTimer);

  int i = 0;
  File gcodeFile = SD.open(uploadfilename.c_str(), FILE_READ);
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

void handleNotFound() {
  String message = "; File Not Found\n";
  message += "; URI: ";
  message += server.uri();
  message += "\n; Method: ";
  message += ( server.method() == HTTP_GET ) ? "GET" : "POST";
  message += "\n; Arguments: ";
  message += server.args();
  message += "\n";

  for ( uint8_t i = 0; i < server.args(); i++ ) {
    message += " " + server.argName ( i ) + ": " + server.arg ( i ) + "\n";
  }

  server.send ( 404, "text/plain", message );
  lcd(server.uri());
}

void handleDownload() {
  File dataFile = SD.open(uploadfilename);
  int fsizeDisk = dataFile.size();

  String WebString = "";
  WebString += "HTTP/1.1 200 OK\r\n";
  WebString += "Content-Type: text/plain\r\n";
  WebString += "Content-Disposition: attachment; filename=\"cache.gcode\"\r\n";
  WebString += "Content-Length: " + String(fsizeDisk) + "\r\n";
  WebString += "\r\n";
  server.sendContent(WebString);

  char buf[1024];
  int siz = dataFile.size();
  while (siz > 0) {
    size_t len = std::min((int)(sizeof(buf) - 1), siz);
    dataFile.read((uint8_t *)buf, len);
    server.client().write((const char*)buf, len);
    siz -= len;
    yield();
  }
  dataFile.close();
}

/* End webserver functions */

// https://forum.arduino.cc/index.php?topic=228884.msg2670971#msg2670971
String IpAddress2String(const IPAddress& ipAddress)
{
  return String(ipAddress[0]) + String(".") + \
         String(ipAddress[1]) + String(".") + \
         String(ipAddress[2]) + String(".") + \
         String(ipAddress[3])  ;
}

void setup() {
  delay(10000); // 3D printer needs this time
  Serial.begin(115200);
  Serial.setTimeout(240000); // How long we wait for "ok" in milliseconds

  if (SD.begin(SS, 50000000)) { // https://github.com/esp8266/Arduino/issues/1853
    hasSD = true;
  } else {
    lcd("SD Card ERROR");
  }

  const size_t bufferLen = 80;
  char ssid[bufferLen];
  char password[bufferLen];
  char host[bufferLen];
  const char *filename = "/config.ini";
  IniFile ini(filename);
  if (!ini.open()) {
    lcd("Missing ini file");
    // Cannot do anything else
    while (1)
      ;
  }

  // Check the file is valid. This can be used to warn if any lines
  // are longer than the buffer.
  if (!ini.validate(ssid, bufferLen)) {
    lcd("Invalid ini file");
    while (1) // Cannot continue
      ;
  }

/*
config.ini
[network]
ssid=________
password=________
hostname=________
*/
 
  if(! ini.getValue("network", "ssid", ssid, bufferLen)) {
    lcd("Read err ssid");
    while (1) // Cannot continue
      ;
  }
  
  if(! ini.getValue("network", "password", password, bufferLen)) {
    lcd("Read err password");
    while (1) // Cannot continue
      ;
  }

  if(! ini.getValue("network", "hostname", host, bufferLen)) {
    lcd("Read err hostname");
    while (1) // Cannot continue
      ;
  }

  String text;

  // Wait for connection
  pinMode(LED_BUILTIN, OUTPUT);     // Initialize the LED_BUILTIN pin as an output
  digitalWrite(LED_BUILTIN, LOW);   // Turn the LED on (Note that LOW is the voltage level
  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid, password);
  while (WiFi.waitForConnectResult() != WL_CONNECTED) {
    lcd("Connection Failed! Rebooting...");
    delay(5000);
    ESP.restart();
  }
  digitalWrite(LED_BUILTIN, HIGH);   // Turn the LED off (Note that LOW is the voltage level
  text = IpAddress2String(WiFi.localIP());

  if (MDNS.begin(host)) {
    MDNS.addService("http", "tcp", 80);
    MDNS.addService("wirelessprint", "tcp", 80);
    MDNS.addServiceTxt("wirelessprint", "tcp", "version", sketch_version);
  }

  delay(1000); // So that we can read the last message on the LCD
  lcd(text); // may be too large to fit on screen

  // Hostname defaults to esp8266-[ChipID]
  ArduinoOTA.setHostname(host);

  ArduinoOTA.begin();

  server.on("/download", HTTP_GET, handleDownload);
  server.on("/", HTTP_GET, handleIndex);
  server.on("/status", HTTP_GET, handleStatus);
  server.on("/print", HTTP_POST, []() {
    returnOK();
  }, handleFileUpload);

  // For Cura
  server.on("/api/print", HTTP_POST, []() {
    returnOK();
  }, handleFileUpload);

  server.onNotFound(handleNotFound);
  server.begin();

  /* Set up the timer to fire every 2 seconds */
  os_timer_setfn(&myTimer, timerCallback, NULL);
  os_timer_arm(&myTimer, timerInterval, true);

}

void loop() {
  server.handleClient();
  ArduinoOTA.handle();
  if (shouldPrint == true) handlePrint();

  /* When the timer has ticked and we are not printing, ask for temperature */
  if ((isPrinting == false) && (tickOccured == true)) {
    sendToPrinter("M105");
    tickOccured = false;
  }

}
