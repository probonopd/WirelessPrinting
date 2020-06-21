#include "StorageFS.h"

StorageFS storageFS;

bool StorageFS::hasSD, 
     StorageFS::hasSPIFFS;
unsigned int StorageFS::maxPathLength;


File StorageFS::open(const String path, const char *openMode) {
  File file;

  if (openMode == NULL || openMode[0] == '\0')
    return file;

  if (hasSD) {
    #if defined(ESP8266)
      file = SD.open(path.c_str(), openMode);
      if (file && file.isDirectory())
        file.rewindDirectory();
    #elif defined(ESP32)
      file = SD.open(path, openMode);
    #endif
  }
  else if (hasSPIFFS) {
    #if defined(ESP8266)
      if (path.endsWith("/")) {
        file = LittleFS.open(path, "r");
      }
      else
        file = LittleFS.open(path, openMode);
    #elif defined(ESP32)
      file = SPIFFS.open(path, openMode);
    #endif
  }

  return file;
}

void StorageFS::remove(const String filename) {
  if (hasSD)
    SD.remove(filename.c_str());
  else if (hasSPIFFS)
    SPIFFS.remove(filename);
}
