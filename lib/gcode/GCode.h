#ifndef GCODE_H
#define GCODE_H

#include <string.h>

#define MAX_SUPPORTED_EXTRUDERS 6       // Number of supported extruder
#define TEMP_NCHAR 12

bool M115ExtractString(char* dest, const char* response, const char* field);
bool M115ExtractBool(const char* response, const char* field);
bool parseTemp(const char* response, const char* whichTemp, float* actual, float* target);
bool parsePrusaHeatingTemp(const char* response, const char* whichTemp, float* actual);

inline bool hasPrefix(const char* pre, const char* str) {
  return strncmp(pre, str, strlen(pre)) == 0;
}

inline int indexOf(const char* str, const char* sub) {
  // https://stackoverflow.com/a/4832
  const char * found = strstr(str, sub);
  if (found == NULL) {
    return -1;
  }
  return found - str;
}

// Parse position responses from printer like
// X:-33.00 Y:-10.00 Z:5.00 E:37.95 Count X:-3300 Y:-1000 Z:2000
inline bool parsePosition(const char* r) {
  return indexOf(r, "X:") != -1 && indexOf(r, "Y:") != -1 &&
         indexOf(r, "Z:") != -1 && indexOf(r, "E:") != -1;
}


#endif //GCODE_H
