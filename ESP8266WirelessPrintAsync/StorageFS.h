#pragma once

#include "FileWrapper.h"

#if defined(ESP8266)
  extern SdFat SD;
#endif

class StorageFS {
  private:
    static bool hasSD,
                hasSPIFFS;
    static unsigned int maxPathLength;

  public:
    inline static void begin(const bool fastSD) {
      #if defined(ESP8266)
        hasSD = SD.begin(SS, fastSD ? SD_SCK_MHZ(50) : SPI_HALF_SPEED); // https://github.com/esp8266/Arduino/issues/1853
      #elif defined(ESP32)
        hasSD = SD.begin(SS, SPI, fastSD ? 50000000 : 4000000);
      #endif
      if (hasSD)
        maxPathLength = 255;
      else {
        hasSPIFFS = SPIFFS.begin();
        #if defined(ESP8266)
          if (hasSPIFFS) {
            fs::FSInfo fs_info;
            maxPathLength = SPIFFS.info(fs_info) ? fs_info.maxPathLength - 1 : 11;
          }
        #elif defined(ESP8266)
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

    static FileWrapper open(const String path, const char *openMode = "r");
    static void remove(const String filename);
};

extern StorageFS storageFS;
