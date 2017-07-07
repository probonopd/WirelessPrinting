#ifndef _INIFILE_H
#define _INIFILE_H

#define INIFILE_VERSION "1.0.0"

// Maximum length for filename, excluding NULL char 26 chars allows an
// 8.3 filename instead and 8.3 directory with a leading slash
#define INI_FILE_MAX_FILENAME_LEN 26

#include "SD.h"

class IniFileState;

class IniFile {
public:
  enum error_t {
    errorNoError = 0,
    errorFileNotFound,
    errorFileNotOpen,
    errorBufferTooSmall,
    errorSeekError,
    errorSectionNotFound,
    errorKeyNotFound,
    errorEndOfFile,
    errorUnknownError,
  };

  static const uint8_t maxFilenameLen;

  // Create an IniFile object. It isn't opened until open() is called on it.
  IniFile(const char* filename, uint8_t mode = FILE_READ,
	  bool caseSensitive = false);
  ~IniFile();

  inline bool open(void); // Returns true if open succeeded
  inline void close(void);

  inline bool isOpen(void) const;

  inline error_t getError(void) const;
  inline void clearError(void) const;
  // Get the file mode (FILE_READ/FILE_WRITE)
  inline uint8_t getMode(void) const;

  // Get the filename asscoiated with the ini file object
  inline const char* getFilename(void) const;

  bool validate(char* buffer, size_t len) const;
  
  // Get value from the file, but split into many short tasks. Return
  // value: false means continue, true means stop. Call getError() to
  // find out if any error
  bool getValue(const char* section, const char* key,
		   char* buffer, size_t len, IniFileState &state) const;

  // Get value, as one big task. Return = true means value is present
  // in buffer
  bool getValue(const char* section, const char* key,
		  char* buffer, size_t len) const; 

  // Get the value as a string, storing the result in a new buffer
  // (not the working buffer)
  bool getValue(const char* section, const char* key,
		   char* buffer, size_t len, char *value, size_t vlen) const;

  // Get a boolean value
  bool getValue(const char* section, const char* key,
		   char* buffer, size_t len, bool& b) const;

  // Get an integer value
  bool getValue(const char* section, const char* key,
		   char* buffer, size_t len, int& val) const;

  // Get a uint16_t value
  bool getValue(const char* section, const char* key,
		   char* buffer, size_t len, uint16_t& val) const;

  // Get a long value
  bool getValue(const char* section, const char* key,
		   char* buffer, size_t len, long& val) const;

  bool getValue(const char* section, const char* key,
		   char* buffer, size_t len, unsigned long& val) const;

  // Get a float value
  bool getValue(const char* section, const char* key,
		   char* buffer, size_t len, float& val) const;

  // Utility function to read a line from a file, make available to all
  //static int8_t readLine(File &file, char *buffer, size_t len, uint32_t &pos);
  static error_t readLine(File &file, char *buffer, size_t len, uint32_t &pos);
  static bool isCommentChar(char c);
  static char* skipWhiteSpace(char* str);
  static void removeTrailingWhiteSpace(char* str);

  bool getCaseSensitive(void) const;
  void setCaseSensitive(bool cs);
  
  protected:
  // True means stop looking, false means not yet found
  bool findSection(const char* section, char* buffer, size_t len,	
		      IniFileState &state) const;
  bool findKey(const char* section, const char* key, char* buffer,
		 size_t len, char** keyptr, IniFileState &state) const;


private:
  char _filename[INI_FILE_MAX_FILENAME_LEN];
  uint8_t _mode;
  mutable error_t _error;
  mutable File _file;
  bool _caseSensitive;
};

bool IniFile::open(void)
{
  if (_file)
    _file.close();
  _file = SD.open(_filename, _mode);
  if (isOpen()) {
    _error = errorNoError;
    return true;
  }
  else {
    _error = errorFileNotFound;
    return false;
  }
}

void IniFile::close(void)
{
  if (_file)
    _file.close();
}

bool IniFile::isOpen(void) const
{
  return (_file == true);
}

IniFile::error_t IniFile::getError(void) const
{
  return _error;
}

void IniFile::clearError(void) const
{
  _error = errorNoError;
}

uint8_t IniFile::getMode(void) const
{
  return _mode;
}

const char* IniFile::getFilename(void) const
{
  return _filename;
}



class IniFileState {
public:
  IniFileState();

private:
  enum {funcUnset = 0,
	funcFindSection,
	funcFindKey,
  };
	
  uint32_t readLinePosition;
  uint8_t getValueState;

  friend class IniFile;
};

  
#endif

