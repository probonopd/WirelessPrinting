#include "GCode.h"
#include <cstdio>
#include <stdlib.h>     /* strtof */


bool asFloat(float* dest, const char* str) {
  // https://stackoverflow.com/a/57163016
  if (strlen(str) == 0)
    return false;

  char* ptr;
  *dest = strtof(str, &ptr);
  return (*ptr) == '\0';
}

bool M115ExtractString(char* dest, const char* r, const char* field) {
  //printf("ExtractString: %s, field %s\n", r, field);
  int spos = indexOf(r, field);
  if (spos == -1) { 
    return false;
  }
  const char* valstart = r + spos + strlen(field) + 1;
  //printf("valstart %c\n", *valstart);
  int epos = indexOf(valstart, ":");
  if (epos == -1) {
    epos = indexOf(valstart, "\n");
  }
  //printf("epos %d\n", epos);
  if (epos == -1) {
    strcpy(dest, valstart); // TODO strncpy
    return true;
  } else {
    while (epos > 0 && valstart[epos] != ' ' && valstart[epos] != '\n') {
      --epos;
    }
    strncpy(dest, valstart, epos);
    return true; 
  }
}

bool M115ExtractBool(const char* r, const char* field) {
  char buf[1];
  if (M115ExtractString(buf, r, field)) {
    return *buf == '1';
  }
  return false;
}

// Parse temperatures from printer responses like
// ok T:32.8 /0.0 B:31.8 /0.0 T0:32.8 /0.0 @:0 B@:0
bool parseTemp(const char* response, const char* whichTemp, float* actual, float* target) {
  int tpos = indexOf(response, whichTemp);
  size_t tlen = strlen(whichTemp);
  if (tpos == -1) {
    return false;
  }
  const char* actual_start = response + tpos + tlen + 1;
  int slashpos = indexOf(actual_start, " /");
  if (slashpos == -1) {
    return false;
  }
  const char* target_start = actual_start + slashpos + 2;
  int spacepos = indexOf(target_start, " ");
  if (spacepos == -1) {
    return false;
  }
  // if match mask T:xxx.xx /xxx.xx
  char buf[TEMP_NCHAR];
  memset(buf, 0, TEMP_NCHAR);
  strncpy(buf, actual_start, slashpos);
  if (!asFloat(actual, buf)) {
    //printf("actual buf '%s' not float\n", buf);
    return false;
  }
  memset(buf, 0, TEMP_NCHAR);
  strncpy(buf, target_start, spacepos-1);
  if (!asFloat(target, buf)) {
    //printf("target buf '%s' not float\n", buf);
    return false;
  }
  return true;
}

// Parse temperatures from prusa firmare (sent when heating)
// ok T:32.8 E:0 B:31.8
bool parsePrusaHeatingTemp(const char* response, const char* whichTemp, float* actual) {
  size_t tlen = strlen(whichTemp);
  int tpos = indexOf(response, whichTemp);
  if (tpos == -1) {
    return false;
  }
  const char* actual_start = response+tpos+tlen+1;
  int spacepos = indexOf(actual_start, " ");
  //printf("tpos %d (%c), spacepos %d (%c)\n", tpos, response[tpos], spacepos, actual_start[spacepos]);

  char buf[TEMP_NCHAR];
  memset(buf, 0, TEMP_NCHAR);
  if (spacepos == -1) {
    strcpy(buf, actual_start);
  } else {
    strncpy(buf, actual_start, spacepos);
  }
  if (!asFloat(actual, buf)) {
    //printf("actual buf '%s' not float\n", buf);
    return false;
  }
  return true;
}

