/** 
  loggerStream package

  $Id: loggerStream.h 130 2009-11-26 18:52:28Z gerhardus $
  Versioning: a.b.c  a is a major release, b represents changes or new additions, c is a bug fix
  @Version 1.0.0   25/03/2008    Gerhardus Muller     Script created

  @note

  @todo

  @bug

  Copyright notice
*/

#ifndef __class_loggerStream_has_been_included__
#define __class_loggerStream_has_been_included__

#include <string>
#include <stdio.h>
#include <pthread.h>
#include <sstream>
#include "logging/loggerDefs.h"
class logger;

class loggerStream : public loggerDefs
{
  public:
    // declarations
    static const int UTILS_MAX_HEX_DIGITS_PER_LINE = 24;

    // methods
  public:
    loggerStream( logger* theLog, priority thePriority, eLogLevel theLogLevel );
    loggerStream( logger* theLog, priority thePriority, eLogLevel theLogLevel, const char* s );
    loggerStream();
    ~loggerStream();
    /**
     * Stream in arbitrary types and objects.  
     * @param t The value or object to stream in.
     * @returns A reference to itself.
     **/
//    template<typename T> loggerStream& operator<<( const std::string& t ) 
//    {
//      fprintf( stderr, "operator 2\n" );
//      if( wouldLog( logLevel ) )
//      {
//        if (!buffer) 
//        {
//          if (!(buffer = new std::ostringstream)) 
//          {
//            fprintf( stderr, "ERROR - loggerStream unable to allocate new ostringstream" );
//            throw "loggerStream unable to allocate new ostringstream";
//          }
//        }
//        if( bDumpAsHex )
//        {
//          bDumpAsHex = false;
//          convertToHex( t );
//        }
//        else
//        {
//          (*buffer) << t;
//        }
//      } // if
//      return *this;
//    } // operator<<

    template<typename T> loggerStream& operator<<( const T& t ) 
    {
      if( wouldLog( logLevel ) )
      {
        if (!buffer) 
        {
          if (!(buffer = new std::ostringstream)) 
          {
            fprintf( stderr, "ERROR - loggerStream unable to allocate new ostringstream" );
            throw "loggerStream unable to allocate new ostringstream";
          }
        }
        (*buffer) << t;
      } // if
      return *this;
    } // operator<<


    loggerStream& operator<<( Modifier separator );
    loggerStream& operator<<( priority p )   {logPriority=p;return *this;}
    loggerStream& operator<<( eLogLevel p )  {logLevel=p;return *this;}
    loggerStream& hexDump( const std::string& str );

  protected:

  private:
    void flush();

    // properties
  public:

  protected:

  private:
    priority                logPriority;
    logger*                 pLog;
  	std::ostringstream*     buffer;
};  // loggerStream

#endif  // #ifndef __class_loggerStream_has_been_included__
