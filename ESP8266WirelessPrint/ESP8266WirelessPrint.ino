/*
   Works with Arduino Hourly and ESP git as of March 4, 2017
   when using a 2 GB card formatted with the SD Association's formatter
   and then creating a file "cache.gco" in Linux (IMPORTANT!)
  Send G-Code stored on SD card
  This sketch reads G-Code from a file from the SD card using the
  SD library and sends it over the serial port.
  Basic G-Code sending:
  http://fightpc.blogspot.de/2016/08/g-code-over-wifi.html
  Advanced G-Code sending:
  https://github.com/Ultimaker/Cura/blob/master/plugins/USBPrinting/USBPrinterOutputDevice.py
  The circuit:
   SD card attached to WeMos D1 mini as follows:
 ** MOSI - pin D7
 ** MISO - pin D6
 ** CLK - pin D5
 ** CS - pin D8
*/

#include <ESP8266WiFi.h>
#include <ESP8266mDNS.h>
#include <WiFiUdp.h>
#include <ArduinoOTA.h>
#include <SPI.h>
#include <SD.h>
#include <ESP8266WebServer.h>

#include "private.h"

#define MTU_Size 2*1460 // https://github.com/esp8266/Arduino/issues/1853

const char* sketch_version = "1.0";

const char* host = "3d";
const int chipSelect = SS;

const String uploadfilename = "cache.gco"; // 8+3 limitation of FAT, otherwise won't be written

bool okFound; // Set to true if last response from 3D printer was "ok", otherwise false
String response; // The last response from 3D printer

bool isPrinting = false;

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
    lcd("Receiving...");
    if (SD.exists((char *)uploadfilename.c_str())) SD.remove((char *)uploadfilename.c_str());
    delay(500);
    uploadFile = SD.open(uploadfilename.c_str(), FILE_WRITE);
    // Serial.print("; Upload: START, filename: "); Serial.println(uploadfilename); // TODO: Convert to lcd()
  } else if (upload.status == UPLOAD_FILE_WRITE) {
    if (uploadFile) uploadFile.write(upload.buf, upload.currentSize);
  } else if (upload.status == UPLOAD_FILE_END) {
    if (uploadFile) uploadFile.close();
    // Serial.print("; Upload: END, Size: "); Serial.println(upload.totalSize); // TODO: Convert to lcd()
    delay(50);
    lcd("Received");
    delay(1000); // So that we can read the message
    handleStart();
  }
}

void handleIndex() {
  // TODO: Don't upload if print is running. Offer stop instead.
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
    message += "<p><a href=\"start\">Print last job again</a></p>";
  }
  server.send(200, "text/html", message);
}

void handleStart() {
  isPrinting = true;
  String output;
  server.setContentLength(CONTENT_LENGTH_UNKNOWN);
  server.send(200, "text/json", "");
  WiFiClient client = server.client();
  client.setNoDelay(true); // https://github.com/esp8266/Arduino/issues/1853#issue-145533999
  output = "Opening file...";
  server.sendContent(output);

  int i = 0;
  File gcodeFile = SD.open(uploadfilename.c_str(), FILE_READ);
  String line;
  if (gcodeFile) {
    while (gcodeFile.available()) {
      i = i + 1;
      line = gcodeFile.readStringUntil('\n'); // The G-Code line being worked on
      int pos = line.indexOf(';');
      if (pos != -1) {
        line = line.substring(0, pos);
      }

      if ((line.startsWith("(")) || (line.startsWith(";")) || (line.length() == 0)) {
        continue;
      }

      Serial.println(line); // Send to 3D Printer

      String myString = "#" + String(i) + "\n--> ";
      server.sendContent(myString);
      server.sendContent(line);

      okFound = false;
      while (okFound == false) {
        response = Serial.readStringUntil('\n');
        server.sendContent("\n<-- " + response);
        if (response.startsWith("ok")) okFound = true;
      }

      server.sendContent("\nGOT OK\n\n");

    }

  } else {
    server.sendContent("The file is not available on the SD card");

  }
  server.sendContent("Complete");
  isPrinting = false;
  lcd("Complete");
}

/*
void handleOctoprintApiPrinter() {
  String message = "{\"state\": { \"text\": \"Operational\", \"flags\": { \"operational\": true, \"paused\": false, \"printing\": false, \"sdReady\": true, \"error\": false, \"ready\": true, \"closedOrError\": false } } }";
  server.send(200, "text/json", message);
  lcd(server.uri());
}

void handleOctoprintApiJob() {
  String message = "{ }";
  server.send(204, "text/json", message);
  lcd(server.uri());
}

void handleOctoprintApiFiles() {
  String message = "{ }";
  server.send(300, "text/json", message);
  lcd(server.uri());
}
*/

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


/* End webserver functions */

// https://forum.arduino.cc/index.php?topic=228884.msg2670971#msg2670971
String IpAddress2String(const IPAddress& ipAddress)
{
  return String(ipAddress[0]) + String(".") + \
         String(ipAddress[1]) + String(".") + \
         String(ipAddress[2]) + String(".") + \
         String(ipAddress[3])  ;
}

void lcd(String string) {
  okFound = false;
  Serial.print("M117 ");
  Serial.println(string);
    while (okFound == false) {
    response = Serial.readStringUntil('\n');
    if (response.startsWith("ok")) okFound = true;
    }
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
    MDNS.addServiceTxt("wirelessprint", "tcp", "useHttps", "false");
    MDNS.addServiceTxt("wirelessprint", "tcp", "path", "/");
    
    /*
     * Since the Cura OctoPrint plugin makes more than one HTTP request at a time, we would need to use
     * https://github.com/me-no-dev/ESPAsyncWebServer#why-should-you-care
     */

    /*
    MDNS.addService("octoprint", "tcp", 80); // https://github.com/fieldOfView/OctoPrintPlugin/blob/master/OctoPrintOutputDevicePlugin.py
    MDNS.addServiceTxt("octoprint", "tcp", "version", sketch_version);
    MDNS.addServiceTxt("octoprint", "tcp", "useHttps", "false");
    MDNS.addServiceTxt("octoprint", "tcp", "path", "/octoprint");
    */
    
    /*
     * FIXME: For this to work, we would need to implement
     * http://docs.octoprint.org/en/master/api/
     * Essentially the answers to what OctoPrintPlugin/OctoPrintOutputDevice.py is looking for
     * printer, files, job
     */

    /*
    MDNS.addService("ultimaker", "tcp", 80); // https://github.com/Ultimaker/Cura/blob/master/plugins/UM3NetworkPrinting/NetworkPrinterOutputDevicePlugin.py
    MDNS.addServiceTxt("ultimaker", "tcp", "type", "printer");
    MDNS.addServiceTxt("ultimaker", "tcp", "name", host);
    MDNS.addServiceTxt("ultimaker", "tcp", "firmware_version", sketch_version);
    MDNS.addServiceTxt("ultimaker", "tcp", "machine", "0000"); 
    */
    
    /*
     * FIXME: For this to work, we would need to implement the UM3 JSON API
     * there is a status dump as a comment in the init method of NetworkPrinterOutputDevice.py
     * WARNING - UM3NetworkPrinting.NetworkPrinterOutputDevice._onFinished [933]: While trying to authenticate, we got an unexpected response: 404
     * WARNING - UM3NetworkPrinting.NetworkPrinterOutputDevice._onFinished [850]: We got an unexpected status (404) while requesting printer state
     */
     
    text = "http://";
    text = text + host;
    text = text + ".local";
  }

  // Hostname defaults to esp8266-[ChipID]
  ArduinoOTA.setHostname(host);

  ArduinoOTA.begin();

  server.on("/start", HTTP_GET, handleStart);
  server.on("/", HTTP_GET, handleIndex);
  server.on("/print", HTTP_POST, []() {
    returnOK();
  }, handleFileUpload);

  // For Cura
  server.on("/api/print", HTTP_POST, []() {
    returnOK();
  }, handleFileUpload);

  /*
  server.on("/octoprint/", HTTP_GET, handleIndex); // Note the trailing slash
  server.on("/octoprint/api/printer", HTTP_GET, handleOctoprintApiPrinter);
  server.on("/octoprint/api/job", HTTP_GET, handleOctoprintApiJob);
  server.on("/octoprint/api/files", HTTP_GET, handleOctoprintApiFiles);
  */

  server.onNotFound(handleNotFound);  
  server.begin();

  if (SD.begin(SS)) { // https://github.com/esp8266/Arduino/issues/1853
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
}
