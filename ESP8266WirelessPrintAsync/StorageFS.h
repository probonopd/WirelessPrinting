#pragma once


#if defined(ESP8266)
  #include <SDFS.h>
  #include <Arduino.h>
  #define SD SDFS
#endif

#if defined(ESP32)
  #include <SD.h>
  #include <SPIFFS.h>
  #include <SPI.h>
#endif

class StorageFS {
  private:
    static bool hasSD,
                hasSPIFFS;
    static unsigned int maxPathLength;

  public:
    inline static void begin(const bool fastSD, int sdpin=SS) {
      #if defined(ESP8266)
        SDFSConfig cfg(sdpin, fastSD ? SD_SCK_MHZ(50) : SPI_HALF_SPEED );
        SDFS.setConfig(cfg);
        hasSD = SDFS.begin(); // https://github.com/esp8266/Arduino/issues/1853
      #elif defined(ESP32)
        SPI.begin(14, 2, 15, 13); // TTGO-T1 V1.3 internal microSD slot
        hasSD = SD.begin(sdpin, SPI, fastSD ? 50000000 : 4000000);
      #endif
      if (hasSD)
        maxPathLength = 255;
      else {
        #if defined(ESP8266)
          hasSPIFFS = SPIFFS.begin();
          if (hasSPIFFS) {
            fs::FSInfo fs_info;
            maxPathLength = SPIFFS.info(fs_info) ? fs_info.maxPathLength - 1 : 11;
          }
        #elif defined(ESP32)
          hasSPIFFS = SPIFFS.begin(true);
          maxPathLength = 11;
        #endif
      }
    }

    inline static bool activeSD() {
      return hasSD;
    }

    inline static bool activeSPIFFS() {
      return hasSPIFFS;
    }

    inline static bool isActive() {
      return activeSD() || activeSPIFFS();
    }

    inline static String getActiveFS() {
      return activeSD() ? "SD" : (activeSPIFFS() ? "SPIFFS" : "NO FS");
    }

    inline static unsigned int getMaxPathLength() {
      return maxPathLength;
    }

    static File open(const String path, const char *openMode = "r");
    static void remove(const String filename);
};

extern StorageFS storageFS;
