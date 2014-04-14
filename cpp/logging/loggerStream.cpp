/** @class loggerStream
 Logging infrastructure
 
 $Id: loggerStream.cpp 1 2009-08-17 12:36:47Z gerhardus $
 Versioning: a.b.c  a is a major release, b represents changes or new additions, c is a bug fix
 @version 1.0.0   25/03/2008    Gerhardus Muller     Script created
 
 @note
 
 @todo
 
 @bug
 
 Copyright notice
 */

#include "logging/logger.h"

/**
 * default constructor - call init later on
 * **/
loggerStream::loggerStream( )
{
  buffer = NULL;
} // loggerStream

loggerStream::loggerStream( logger* theLog, priority thePriority, eLogLevel theLogLevel ) 
  : logPriority( thePriority ),
    pLog( theLog ),
    buffer( NULL )
{
  logLevel = theLogLevel;
} // loggerStream
loggerStream::loggerStream( logger* theLog, priority thePriority, eLogLevel theLogLevel, const char* s ) 
  : logPriority( thePriority ),
    pLog( theLog ),
    buffer( NULL )
{
  logLevel = theLogLevel;

  if( wouldLog( logLevel ) )
  {
    if (!(buffer = new std::ostringstream)) 
      throw "loggerStream unable to allocate new ostringstream";
    (*buffer) << s;
  } // if
} // loggerStream

/** 
 * destructor
 * */
loggerStream::~loggerStream()
{
  flush( );
} // ~loggerStream

/**
 * Streams in a Modifier. If the separator equals 
 * CategoryStream::ENDLINE it sends the contents of the stream buffer
 * to the Category with set priority and empties the buffer.
 * @param separator The stream modifier
 * @returns A reference to itself.
 **/
loggerStream& loggerStream::operator<<(Modifier separator)
{
  switch( separator )
  {
    case ENDLINE:
    case EOL:
      flush();
      break;
  } // switch
  return *this;
} // operator<<

/**
  Convert a string to hex - the client to supply the buffer
  @param str - contains the data to dump - does not need to be printable
  Ex: (log.debug() << "buffer: ").hexDump( std::string( (const char*)s, numRead ) );
  */
loggerStream& loggerStream::hexDump( const std::string& str )
{
  char lineBuf[256];
  int lenToPrint = str.length();
  if( lenToPrint > UTILS_MAX_HEX_DIGITS_PER_LINE ) (*buffer) << "\n";
  
  int row;
  int index = 0;
  for (row = 0; index < lenToPrint; row++ )
  {
    sprintf( lineBuf, "0x%03x: ", index );
    (*buffer) << lineBuf;
    for (int i = 0; (i < UTILS_MAX_HEX_DIGITS_PER_LINE) && (index+i < lenToPrint); i++)
    {
      sprintf( lineBuf, "%02x ", 0xff & str[index+i] );
      (*buffer) << lineBuf;
    } // for
    (*buffer) << ": ";
    for (int i = 0; (i < UTILS_MAX_HEX_DIGITS_PER_LINE) && (index+i < lenToPrint); i++)
    {
      if( isprint( str[index+i] ) )
        (*buffer) << str[index+i];
      else
        (*buffer) << ' ';
    } // for
    (*buffer) << "\n";
    index += UTILS_MAX_HEX_DIGITS_PER_LINE;
  } // for

  // remove the trailing CR
  std::string tmpString = buffer->str();
  tmpString.erase( tmpString.length()-1 );
  buffer->str( tmpString );
  return *this;
} // hexDump

/**
 * Flush the contents of the stream buffer to the Category and
 * empties the buffer.
 **/
void loggerStream::flush( )
{
  if( buffer )
  {
    pLog->log( logPriority, logLevel, buffer->str() );
    delete buffer;
    buffer = NULL;
  } // if 
} // flush
