#include "FileWrapper.h"
#include "StorageFS.h"

size_t FileWrapper::write(uint8_t b) {
  uint8_t buf[] = { b };
  
  return write(buf, 1);
}

size_t FileWrapper::write(const uint8_t *buf, size_t len) {
  if (sdFile)
    return sdFile.write(buf, len);
  else if (fsFile) {
    #if defined(ESP8266)
      ESP.wdtDisable();
    #endif
    size_t wb = fsFile.write(buf, len);
    #if defined(ESP8266)
      ESP.wdtEnable(250);
    #endif
    return wb;
  }

  return 0;
}

void FileWrapper::flush() {
  if (sdFile)
    return sdFile.flush();
  else if (fsFile)
    return fsFile.flush();
}

int FileWrapper::available() {
  return sdFile ? sdFile.available() : (fsFile ? fsFile.available() : false);
}

int FileWrapper::peek() {
  if (sdFile)
    return sdFile.peek();
  else if (fsFile)
    return fsFile.peek();

  return -1;
}

int FileWrapper::read() {
  if (sdFile)
    return sdFile.read();
  else if (fsFile)
    return fsFile.read();

  return -1;
}

String FileWrapper::name() {
  if (sdFile) {
    #if defined(ESP8266)
      if (cachedName == "") {
        const int maxPathLength = StorageFS::getMaxPathLength();
        char *namePtr = (char *)malloc(maxPathLength + 1);
        sdFile.getName(namePtr, maxPathLength);
        cachedName = String(namePtr);
        free (namePtr);
      }

      return cachedName;
    #elif defined(ESP32)
      return sdFile.name();
    #endif
  }
  else {
    #if defined(ESP8266)
      if (fsDirType != DirEntry)
        return "";

      String name = fsDir.fileName();
      int i = name.lastIndexOf("/");

      return i == -1 ? name : name.substring(i + 1);
    #elif defined(ESP32)
      return fsFile.name();
    #endif
  }
}

uint32_t FileWrapper::size() {
  if (sdFile)
    return sdFile.size();
  else if (fsFile)
    return fsFile.size();
#if defined(ESP8266)
  else if (fsDirType == DirEntry)
    return fsDir.fileSize();
#endif

  return 0;
}

size_t FileWrapper::read(uint8_t *buf, size_t size) {
  if (sdFile)
    return sdFile.read(buf, size);
  else if (fsFile)
    return fsFile.read(buf, size);

  return 0;
}

String FileWrapper::readStringUntil(char eol) {
  return sdFile ? sdFile.readStringUntil(eol) : (fsFile ? fsFile.readStringUntil(eol) : "");
}

void FileWrapper::close() {
  if (sdFile) {
    sdFile.close();
    sdFile = File();
  }
  else if (fsFile) {
    fsFile.close();
    fsFile = fs::File();
  }
#if defined(ESP8266)
  else if (fsDirType != Null) {
    fsDir = fs::Dir();
    fsDirType = Null;
  }
#endif
}

FileWrapper FileWrapper::openNextFile() {
  FileWrapper fw = FileWrapper();

  if (sdFile)
    fw.sdFile = sdFile.openNextFile();
#if defined(ESP8266)
  else if (fsDirType == DirSource) {
    if (fsDir.next()) {
      fw.fsDir = fsDir;
      fw.fsDirType = DirEntry;
    }
  }
#elif defined(ESP32)
  else if (fsFile)
    fw.fsFile = fsFile.openNextFile();
#endif

  return fw;
}
