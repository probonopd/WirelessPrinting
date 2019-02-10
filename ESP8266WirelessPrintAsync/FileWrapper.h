#pragma once

#define FS_NO_GLOBALS //allow spiffs to coexist with SD card, define BEFORE including FS.h

#include <FS.h>
#include <SdFat.h>

class FileWrapper {
  friend class StorageFS;

  private:
    static const int MaxPathLength = 255;
    enum FSDirType { Null, DirSource, DirEntry };

    File sdFile;
    fs::File fsFile;
    fs::Dir fsDir;
    FSDirType fsDirType;

  public:
    inline operator bool() {
      return sdFile || fsFile || fsDirType != Null;
    }

    String name();
    bool available();
    void close();
    long size();
    String readStringUntil(char eol);
    void write(const uint8_t *buf, size_t len);

    inline bool isDirectory() {
      return sdFile ? sdFile.isDirectory() : (fsDirType == DirSource);
    }

    FileWrapper openNextFile();
};
