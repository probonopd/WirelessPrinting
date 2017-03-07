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

#include "private.h"

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

String sendToPrinter(String line) {
  
      Serial.println(line); // Send to 3D Printer
      
      okFound = false;
      while (okFound == false) {
        response = Serial.readStringUntil('\n');
        if (response.startsWith("ok")) okFound = true;
      }
      return(response);
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

void handleFileUpload() {
  lcd("Receiving...");
  HTTPUpload& upload = server.upload();
  if (upload.status == UPLOAD_FILE_START) {
    if (SD.exists((char *)uploadfilename.c_str())) SD.remove((char *)uploadfilename.c_str());
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

  server.on("/download", HTTP_GET, handleDownload);
  server.on("/", HTTP_GET, handleIndex);
  server.on("/print", HTTP_POST, []() {
    returnOK();
  }, handleFileUpload);

  // For Cura
  server.on("/api/print", HTTP_POST, []() {
    returnOK();
  }, handleFileUpload);

  server.onNotFound(handleNotFound);
  server.begin();

  if (SD.begin(SS, 50000000)) { // https://github.com/esp8266/Arduino/issues/1853
    hasSD = true;
    lcd("SD Card OK");
    delay(1000); // So that we can read the last message on the LCD
    lcd(text); // may be too large to fit on screen
  } else {
    lcd("SD Card ERROR");
  }
}

void loop() {
  server.handleClient();
  ArduinoOTA.handle();
  if (shouldPrint == true) handlePrint();
}
