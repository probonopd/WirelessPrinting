#pragma once

#include "FileWrapper.h"

extern SdFat SD;

class StorageFS {
  private:
    static bool hasSD,
                hasSPIFFS;
    static unsigned int maxPathLength;

  public:
    inline static void begin(const bool fastSD) {
      hasSD = SD.begin(SS, fastSD ? SD_SCK_MHZ(50) : SPI_HALF_SPEED); // https://github.com/esp8266/Arduino/issues/1853
      if (hasSD)
        maxPathLength = 255;
      else {
        hasSPIFFS = SPIFFS.begin();
        if (hasSPIFFS) {
          fs::FSInfo fs_info;
          maxPathLength = SPIFFS.info(fs_info) ? fs_info.maxPathLength - 1 : 11;
          hasSPIFFS = true;
        }
      }
    }

    inline static bool activeSD() {
      return hasSD;
    }

    inline static bool activeSPIFFS() {
      return hasSPIFFS;
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
