#pragma once

#define FS_NO_GLOBALS // allow spiffs to coexist with SD card, define BEFORE including FS.h
#include <FS.h>
#if defined(ESP8266)
  #include <SdFat.h>
#elif defined(ESP32)
  #include <SPIFFS.h>
  #define FORMAT_SPIFFS_IF_FAILED true
  #include <SD.h>
#endif

class FileWrapper : public Stream {
  friend class StorageFS;

  private:
    File sdFile;
    fs::File fsFile;
    #if defined(ESP8266)
      enum FSDirType { Null, DirSource, DirEntry };

      String cachedName;
      fs::Dir fsDir;
      FSDirType fsDirType;
    #endif

  public:
    // Print methods
    virtual size_t write(uint8_t datum);
    virtual size_t write(const uint8_t *buf, size_t size);

    // Stream methods
    virtual void flush();
    virtual int available();
    virtual int peek();
    virtual int read();

    inline operator bool() {
      return sdFile || fsFile
      #if defined(ESP8266)
        || fsDirType != Null;
      #endif
      ;
    }

    String name();
    uint32_t size();
    size_t read(uint8_t *buf, size_t size);
    String readStringUntil(char eol);
    void close();

    inline bool isDirectory() {
      if (sdFile)
        return sdFile.isDirectory();
      #if defined(ESP8266)
        return fsDirType == DirSource;
      #else
        return fsFile ? fsFile.isDirectory() : false;
      #endif
    }

    FileWrapper openNextFile();
};
