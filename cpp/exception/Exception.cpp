/** @class Exception
 general Exception class
 
 $Id: Exception.cpp 1 2009-08-17 12:36:47Z gerhardus $
 Versioning: a.b.c  a is a major release, b represents changes or new additions, c is a bug fix
 @version 1.0.0   03/07/2007    Gert Muller     derived from the equivalent version in mserver
 
 @note
 
 @todo
 
 @bug
 
 Copyright notice
 */

#include "exception/Exception.h"
#include <stdio.h>
#include <stdarg.h>
#include <cstring>

// use loggerDefs::DEBUG loggerDefs::INFO loggerDefs::WARN loggerDefs::ERROR
Exception::Exception( logger &theLogger, loggerDefs::priority thePriority, const char* theStr, ... ) throw()
{
  va_list args;
  va_start( args, theStr );
  char intBuffer[BUFLEN];
  
  vsnprintf( intBuffer, BUFLEN, theStr, args );
  intBuffer[BUFLEN-1] = '\0';
  str = intBuffer;
  theLogger.log( thePriority, loggerDefs::MINLEVEL, intBuffer );
  va_end( args );
} // Exception

Exception::Exception( logger &theLogger, loggerDefs::priority thePriority, const std::string& theExceptionStr ) 
{
  str = theExceptionStr; 
  theLogger.log( thePriority, loggerDefs::MINLEVEL, str.c_str() );
} // Exception

Exception::Exception( logger &theLogger, loggerDefs::priority thePriority, int theLineNo, const char*, const char* theFileName, const char* theStr, ... )
{
  va_list args;
  va_start( args, theStr );
  char intBuffer[BUFLEN];
  snprintf( intBuffer, BUFLEN, "%s line %d: ", theFileName, theLineNo );
  intBuffer[BUFLEN-1] = '\0';
  int len = strlen( intBuffer );
  
  vsnprintf( &intBuffer[len], BUFLEN-len, theStr, args );
  intBuffer[BUFLEN-1] = '\0';
  str = intBuffer;
  theLogger.log( thePriority, loggerDefs::MINLEVEL, intBuffer );
  va_end( args );
} // Exception
 
/**
 Destructor - empty for the base class
 */
Exception::~Exception()
{
} // ~Exception

