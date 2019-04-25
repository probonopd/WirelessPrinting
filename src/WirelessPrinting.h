#ifndef WirelessPrinting_h
#define WirelessPrinting_h

#include <ArduinoOTA.h>
#if defined(ESP8266)
  #include <ESP8266mDNS.h>        // https://github.com/esp8266/Arduino/tree/master/libraries/ESP8266mDNS
#elif defined(ESP32)
  #include <ESPmDNS.h>
  #include <Update.h>
#endif
#include <ArduinoJson.h>          // https://github.com/bblanchon/ArduinoJson (for implementing a subset of the OctoPrint API)
#include <DNSServer.h>
#include "StorageFS.h"
#include <ESPAsyncWebServer.h>    // https://github.com/me-no-dev/ESPAsyncWebServer
#include <ESPAsyncWiFiManager.h>  // https://github.com/alanswx/ESPAsyncWiFiManager/
#include <SPIFFSEditor.h>

#include "CommandQueue.h"

// On ESP8266 use the normal Serial() for now, but name it PrinterSerial for compatibility with ESP32
// On ESP32, use Serial1 (rather than the normal Serial0 which prints stuff during boot that confuses the printer)
#ifdef ESP8266
#define PrinterSerial Serial
#endif
#ifdef ESP32
HardwareSerial PrinterSerial(1);
#endif

// Configurable parameters
#define SKETCH_VERSION "2.x-localbuild" // Gets inserted at build time by .travis.yml
#define USE_FAST_SD                     // Use Default fast SD clock, comment if your SD is an old or slow one.
#define OTA_UPDATES                     // Enable OTA firmware updates, comment if you don't want it (OTA may lead to security issues because someone may load any code on device)
//#define OTA_PASSWORD ""               // Uncomment to protect OTA updates and assign a password (inside "")
#define MAX_SUPPORTED_EXTRUDERS 6       // Number of supported extruder
#define REPEAT_M115_TIMES 1             // M115 retries with same baud (MAX 255)

#define PRINTER_RX_BUFFER_SIZE 0        // This is printer firmware 'RX_BUFFER_SIZE'. If such parameter is unknown please use 0
#define TEMPERATURE_REPORT_INTERVAL 2   // Ask the printer for its temperatures status every 2 seconds
#define KEEPALIVE_INTERVAL 2500         // Marlin defaults to 2 seconds, get a little of margin
const uint32_t serialBauds[] = { 115200, 250000, 57600 };    // Marlin valid bauds (removed very low bauds; roughly ordered by popularity to speed things up)

#define API_VERSION     "0.1"
#define VERSION         "1.3.10"

// Temperature for printer status reporting
#define TEMP_COMMAND      "M105"
#define AUTOTEMP_COMMAND  "M155 S"

struct Temperature {
String actual, target;
};



class WirelessPrintingClass{
    public:
        void init(AsyncWebServer& server);
        void loop();

    protected:
        // Helper Functions
        inline String IpAddress2String(const IPAddress& ipAddress);
        inline void setLed(const bool status);
        inline void telnetSend(const String line);
        bool isFloat(const String value);
        bool parseTemp(const String response, const String whichTemp, Temperature *temperature);
        bool parsePrusaHeatingTemp(const String response, const String whichTemp, Temperature *temperature);
        int8_t parsePrusaHeatingExtruder(const String response);
        bool parseTemperatures(const String response);
        inline bool parsePosition(const String response);
        String M115ExtractString(const String response, const String field);
        bool M115ExtractBool(const String response, const String field, const bool onErrorValue = false);
        inline String getDeviceName();
        inline String getState();
        inline String stringify(bool value);
        

    private:
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

        uint32_t temperatureTimer;

        Temperature toolTemperature[MAX_SUPPORTED_EXTRUDERS];
        Temperature bedTemperature;

        inline void lcd(const String text);
        inline void playSound();
        inline String getUploadedFilename();
        void handlePrint();
        void handleUpload(AsyncWebServerRequest *request, String filename, size_t index, uint8_t *data, size_t len, bool final);
        int apiJobHandler(JsonObject root);
        void mDNSInit();
        bool detectPrinter();
        void initUploadedFilename();
        inline void restartSerialTimeout();
        void SendCommands();
        void ReceiveResponses();
};

extern WirelessPrintingClass WirelessPrinting;

#endif