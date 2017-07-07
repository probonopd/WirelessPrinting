#include "IniFile.h"

#include <string.h>

const uint8_t IniFile::maxFilenameLen = INI_FILE_MAX_FILENAME_LEN;

IniFile::IniFile(const char* filename, uint8_t mode,
		 bool caseSensitive)
{
  if (strlen(filename) <= maxFilenameLen)
    strcpy(_filename, filename);
  else
    _filename[0] = '\0';
  _mode = mode;
  _caseSensitive = caseSensitive;
}

IniFile::~IniFile()
{
  //if (_file)
  //  _file.close();
}


bool IniFile::validate(char* buffer, size_t len) const
{
  uint32_t pos = 0;
  error_t err;
  while ((err = readLine(_file, buffer, len, pos)) == errorNoError)
    ;
  if (err == errorEndOfFile) {
    _error = errorNoError;
    return true;
  }
  else {
    _error = err;
    return false;
  }
}

bool IniFile::getValue(const char* section, const char* key,
			  char* buffer, size_t len, IniFileState &state) const
{
  bool done = false;
  if (!_file) {
    _error = errorFileNotOpen;
    return true;
  }
  
  switch (state.getValueState) {
  case IniFileState::funcUnset:
    state.getValueState = (section == NULL ? IniFileState::funcFindKey
			   : IniFileState::funcFindSection);
    state.readLinePosition = 0;
    break;
    
  case IniFileState::funcFindSection:
    if (findSection(section, buffer, len, state)) {
      if (_error != errorNoError)
	return true;
      state.getValueState = IniFileState::funcFindKey;
    }
    break;
    
  case IniFileState::funcFindKey:
    char *cp;
    if (findKey(section, key, buffer, len, &cp, state)) {
      if (_error != errorNoError)
	return true;
      // Found key line in correct section
      cp = skipWhiteSpace(cp);
      removeTrailingWhiteSpace(cp);

      // Copy from cp to buffer, but the strings overlap so strcpy is out
      while (*cp != '\0')
	*buffer++ = *cp++;
      *buffer = '\0';
      return true;
    }
    break;
    
  default:
    // How did this happen?
    _error = errorUnknownError;
    done = true;
    break;
  }
  
  return done;
}

bool IniFile::getValue(const char* section, const char* key,
			  char* buffer, size_t len) const
{
  IniFileState state;
  while (!getValue(section, key, buffer, len, state))
    ;
  return _error == errorNoError;
}


bool IniFile::getValue(const char* section, const char* key,
			 char* buffer, size_t len, char *value, size_t vlen) const
{
  if (getValue(section, key, buffer, len) < 0)
    return false; // error
  if (strlen(buffer) >= vlen)
    return false;
  strcpy(value, buffer);
  return true;
}


// For true accept: true, yes, 1
 // For false accept: false, no, 0
bool IniFile::getValue(const char* section, const char* key, 
			  char* buffer, size_t len, bool& val) const
{
  if (getValue(section, key, buffer, len) < 0)
    return false; // error
  
  if (strcasecmp(buffer, "true") == 0 ||
      strcasecmp(buffer, "yes") == 0 ||
      strcasecmp(buffer, "1") == 0) {
    val = true;
    return true;
  }
  if (strcasecmp(buffer, "false") == 0 ||
      strcasecmp(buffer, "no") == 0 ||
      strcasecmp(buffer, "0") == 0) {
    val = false;
    return true;
  }
  return false; // does not match any known strings      
}

bool IniFile::getValue(const char* section, const char* key,
			  char* buffer, size_t len, int& val) const
{
  if (getValue(section, key, buffer, len) < 0)
    return false; // error
  
  val = atoi(buffer);
  return true;
}

bool IniFile::getValue(const char* section, const char* key,	\
			  char* buffer, size_t len, uint16_t& val) const
{
  long longval;
  bool r = getValue(section, key, buffer, len, longval);
  if (r)
    val = uint16_t(longval);
  return r;
}

bool IniFile::getValue(const char* section, const char* key,
			  char* buffer, size_t len, long& val) const
{
  if (getValue(section, key, buffer, len) < 0)
    return false; // error
  
  val = atol(buffer);
  return true;
}

bool IniFile::getValue(const char* section, const char* key,
			  char* buffer, size_t len, unsigned long& val) const
{
  if (getValue(section, key, buffer, len) < 0)
    return false; // error

  char *endptr;
  unsigned long tmp = strtoul(buffer, &endptr, 10);
  if (endptr == buffer)
    return false; // no conversion
  if (*endptr == '\0') {
    val = tmp;
    return true; // valid conversion
  }
  // buffer has trailing non-numeric characters, and since the buffer
  // already had whitespace removed discard the entire results
  return false; 
}


bool IniFile::getValue(const char* section, const char* key,
			  char* buffer, size_t len, float & val) const
{
  if (getValue(section, key, buffer, len) < 0)
    return false; // error

  char *endptr;
  float tmp = strtod(buffer, &endptr);
  if (endptr == buffer)
    return false; // no conversion
  if (*endptr == '\0') {
    val = tmp;
    return true; // valid conversion
  }
  // buffer has trailing non-numeric characters, and since the buffer
  // already had whitespace removed discard the entire results
  return false; 
}


//int8_t IniFile::readLine(File &file, char *buffer, size_t len, uint32_t &pos)
IniFile::error_t IniFile::readLine(File &file, char *buffer, size_t len, uint32_t &pos)
{
  if (!file)
    return errorFileNotOpen;
 
  if (len < 3) 
    return errorBufferTooSmall;

  if (!file.seek(pos))
    return errorSeekError;

  size_t bytesRead = file.read(buffer, len);
  if (!bytesRead) {
    buffer[0] = '\0';
    //return 1; // done
    return errorEndOfFile;
  }
  
  for (size_t i = 0; i < bytesRead && i < len-1; ++i) {
    // Test for '\n' with optional '\r' too
    // if (endOfLineTest(buffer, len, i, '\n', '\r')
	
    if (buffer[i] == '\n' || buffer[i] == '\r') {
      char match = buffer[i];
      char otherNewline = (match == '\n' ? '\r' : '\n'); 
      // end of line, discard any trailing character of the other sort
      // of newline
      buffer[i] = '\0';
      
      if (buffer[i+1] == otherNewline)
	++i;
      pos += (i + 1); // skip past newline(s)
      //return (i+1 == bytesRead && !file.available());
      return errorNoError;
    }
  }
  if (!file.available()) {
    // end of file without a newline
    buffer[bytesRead] = '\0';
    // return 1; //done
    return errorEndOfFile;
  }
  
  buffer[len-1] = '\0'; // terminate the string
  return errorBufferTooSmall;
}

bool IniFile::isCommentChar(char c)
{
  return (c == ';' || c == '#');
}

char* IniFile::skipWhiteSpace(char* str)
{
  char *cp = str;
  while (isspace(*cp))
    ++cp;
  return cp;
}

void IniFile::removeTrailingWhiteSpace(char* str)
{
  char *cp = str + strlen(str) - 1;
  while (cp >= str && isspace(*cp))
    *cp-- = '\0';
}

bool IniFile::findSection(const char* section, char* buffer, size_t len, 
			     IniFileState &state) const
{
  if (section == NULL) {
    _error = errorSectionNotFound;
    return true;
  }

  error_t err = IniFile::readLine(_file, buffer, len, state.readLinePosition);
  
  if (err != errorNoError && err != errorEndOfFile) {
    // Signal to caller to stop looking and any error value
    _error = err;
    return true;
  }
    
  char *cp = skipWhiteSpace(buffer);
  //if (isCommentChar(*cp))
  //return (done ? errorSectionNotFound : 0);
  if (isCommentChar(*cp)) {
    // return (err == errorEndOfFile ? errorSectionNotFound : errorNoError);
    if (err == errorSectionNotFound) {
      _error = err;
      return true;
    }
    else
      return false; // Continue searching
  }
  
  if (*cp == '[') {
    // Start of section
    ++cp;
    cp = skipWhiteSpace(cp);
    char *ep = strchr(cp, ']');
    if (ep != NULL) {
      *ep = '\0'; // make ] be end of string
      removeTrailingWhiteSpace(cp);
      if (_caseSensitive) {
	if (strcmp(cp, section) == 0) {
	  _error = errorNoError;
	  return true;
	}
      }
      else {
	if (strcasecmp(cp, section) == 0) {
	  _error = errorNoError;
	  return true;
	}
      }
    }
  }
  
  // Not a valid section line
  //return (done ? errorSectionNotFound : 0);
  if (err == errorEndOfFile) {
    _error = errorSectionNotFound;
    return true;
  }
  
  return false;
}

// From the current file location look for the matching key. If
// section is non-NULL don't look in the next section
bool IniFile::findKey(const char* section, const char* key,
			 char* buffer, size_t len, char** keyptr,
			 IniFileState &state) const
{
  if (key == NULL || *key == '\0') {
    _error = errorKeyNotFound;
    return true;
  }

  error_t err = IniFile::readLine(_file, buffer, len, state.readLinePosition);
  if (err != errorNoError && err != errorEndOfFile) {
    _error = err;
    return true;
  }
  
  char *cp = skipWhiteSpace(buffer);
  // if (isCommentChar(*cp))
  //   return (done ? errorKeyNotFound : 0);
  if (isCommentChar(*cp)) {
    if (err == errorEndOfFile) {
      _error = errorKeyNotFound;
      return true;
    }
    else
      return false; // Continue searching
  }
  
  if (section && *cp == '[') {
    // Start of a new section
    _error = errorKeyNotFound;
    return true;
  }
  
  // Find '='
  char *ep = strchr(cp, '=');
  if (ep != NULL) {
    *ep = '\0'; // make = be the end of string
    removeTrailingWhiteSpace(cp);
    if (_caseSensitive) {
      if (strcmp(cp, key) == 0) {
	*keyptr = ep + 1;
	_error = errorNoError;
	return true;
      }
    }
    else {
      if (strcasecmp(cp, key) == 0) {
	*keyptr = ep + 1;
	_error = errorNoError;
	return true;
      }
    }
  }

  // Not the valid key line
  if (err == errorEndOfFile) {
    _error = errorKeyNotFound;
    return true;
  }
  return false;
}

bool IniFile::getCaseSensitive(void) const
{
  return _caseSensitive;
}

void IniFile::setCaseSensitive(bool cs)
{
  _caseSensitive = cs;
}

IniFileState::IniFileState()
{
  readLinePosition = 0;
  getValueState = funcUnset;
}
