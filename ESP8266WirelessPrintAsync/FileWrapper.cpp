#include "FileWrapper.h"
#include "StorageFS.h"

String FileWrapper::name() {
  if (sdFile) {
    if (cachedName == "") {
      const int maxPathLength = StorageFS::getMaxPathLength();
      char *namePtr = (char *)malloc(maxPathLength + 1);
      sdFile.getName(namePtr, maxPathLength);
      cachedName = String(namePtr);
      free (namePtr);
      }

    return cachedName;
  }
  else if (fsDirType != DirEntry)
    return "";

  String name = fsDir.fileName();
  int i = name.lastIndexOf("/");

  return i == -1 ? name : name.substring(i + 1);
}

bool FileWrapper::available() {
  return sdFile ? sdFile.available() : (fsFile ? fsFile.available() : false);
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
 else if (fsDirType != Null) {
    fsDir = fs::Dir();
    fsDirType = Null;
  }
}

long FileWrapper::size() {
  if (sdFile)
    return sdFile.size();
  else if (fsFile)
    return fsFile.size();
  else if (fsDirType == DirEntry)
    return fsDir.fileSize();

  return 0;
}

String FileWrapper::readStringUntil(char eol) {
  return sdFile ? sdFile.readStringUntil(eol) : (fsFile ? fsFile.readStringUntil(eol) : "");
}

void FileWrapper::write(const uint8_t *buf, size_t len) {
  if (sdFile) {
    for (size_t i = 0; i < len; i++)
      sdFile.write(buf[i]);
  }
  else if (fsFile) {
    ESP.wdtDisable();
    fsFile.write(buf, len);
    ESP.wdtEnable(250);
  }
}

FileWrapper FileWrapper::openNextFile() {
  FileWrapper fw = FileWrapper();

  if (sdFile)
    fw.sdFile = sdFile.openNextFile();
  else if (fsDirType == DirSource) {
    if (fsDir.next()) {
      fw.fsDir = fsDir;
      fw.fsDirType = DirEntry;
    }
  }

  return fw;
}
