#include "StorageFS.h"

SdFat SD;
StorageFS storageFS;

bool StorageFS::hasSD, 
     StorageFS::hasSPIFFS;
unsigned int StorageFS::maxPathLength;


FileWrapper StorageFS::open(const String path, const char *openMode) {
  FileWrapper file;

  if (openMode == NULL || openMode[0] == '\0')
    return file;

  if (hasSD) {
    const char *pathStr = path.c_str();
    const bool isWriteMode = (openMode[0] == 'w');

    file.sdFile = SD.open(pathStr, isWriteMode ? (O_WRITE | O_CREAT | O_TRUNC) : FILE_READ);
    if (file && file.sdFile.isDirectory())
      file.sdFile.rewindDirectory();
  }
  else if (hasSPIFFS) {
    #if defined(ESP8266)
      if (path.endsWith("/")) {
        file.fsDir = SPIFFS.openDir(path);
        file.fsDirType = FileWrapper::DirSource;
      }
      else
        file.fsFile = SPIFFS.open(path, openMode);
    #elif defined(ESP32)
      file.fsFile = SPIFFS.open(path, openMode);
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
