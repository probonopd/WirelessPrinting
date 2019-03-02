#pragma once

#define FS_NO_GLOBALS //allow spiffs to coexist with SD card, define BEFORE including FS.h
#include <FS.h>
#include <SdFat.h>

class FileWrapper : public Stream {
  friend class StorageFS;

  private:
    enum FSDirType { Null, DirSource, DirEntry };

    File sdFile;
    String cachedName;
    fs::File fsFile;
    fs::Dir fsDir;
    FSDirType fsDirType;

  public:
    // Print methods
    virtual size_t write(uint8_t datum);
    virtual size_t write(const uint8_t *buf, size_t size);

    // Stream methods
    virtual int available();
    virtual int peek();
    virtual int read();

    inline operator bool() {
      return sdFile || fsFile || fsDirType != Null;
    }

    String name();
    uint32_t size();
    size_t read(uint8_t *buf, size_t size);
    String readStringUntil(char eol);
    void close();

    inline bool isDirectory() {
      return sdFile ? sdFile.isDirectory() : (fsDirType == DirSource);
    }

    FileWrapper openNextFile();
};
