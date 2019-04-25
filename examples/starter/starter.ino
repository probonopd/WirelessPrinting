#include <ESP8266WiFi.h>
#include <ESP8266mDNS.h>
#include <ArduinoOTA.h>
#include <FS.h>
#include <Hash.h>
#include <ESPAsyncTCP.h>
#include <ESPAsyncWebServer.h>
#include <SPIFFSEditor.h>
#include <WirelessPrinting.h>

AsyncWebServer server(80);

void setup() {
  Serial.begin(115200);
  WirelessPrinting.begin(server);
  
}

void loop() {
  WirelessPrinting.loop();
}