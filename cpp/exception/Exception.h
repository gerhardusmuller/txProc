/* @class Exception
 general Exception class
 
 $Id: Exception.h 1 2009-08-17 12:36:47Z gerhardus $
 Versioning: a.b.c  a is a major release, b represents changes or new additions, c is a bug fix
 @Version 1.0.0   03/07/2007    Gert Muller     Taken from the mserver version
 
 @note
 
 @todo
 
 @bug
 
 Copyright notice
 */

#ifndef Exception_class
#define Exception_class

#include "logging/logger.h"

class Exception
{
public:
  /**
    Construct a new Exception, logs the Exception - takes a printf style input
    @param theLogger - local logger object
    @param thePriority - logger::priority::DEBUG priority::INFO priority::WARN priority::ERROR
    @param theStr - printf string describing the Exception, followed by the arguments
    */
  Exception( logger &theLogger, loggerDefs::priority thePriority, const char* theStr, ... ) throw();
  /**
    Construct a new Exception, logs the Exception
    @param theLogger - local logger object
    @param thePriority - logger::priority::DEBUG priority::INFO priority::WARN priority::ERROR
    @param theExceptionStr - string describing the Exception
    */
  Exception( logger &theLogger, loggerDefs::priority thePriority, const std::string& theExceptionStr );
  /**
    Construct a new Exception containing file and line info followed by a printf style input
    @param theLogger - local logger object
    @param thePriority - logger::priority::DEBUG priority::INFO priority::WARN priority::ERROR
    @param theLineNo - use __LINE__ macro as parameter
    @param theFileName - use __FILE__ macro as parameter
    @param theStr - printf string describing the Exception, followed by the arguments
    */
  Exception( logger &theLogger, loggerDefs::priority thePriority, int theLineNo, const char*, const char* theFileName, const char* theStr, ... );
    
  Exception( ){};
  virtual ~Exception( );

  const char* getMessage() {return str.c_str();};
  std::string getString() {return str;};

protected:
  
protected:
  static const int BUFLEN = 2048; ///< max length of the log message
  std::string str;                ///< holds the error message
}; // class Exception

#endif

