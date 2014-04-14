/** 
 logger package
 
 $Id: logger.h 2880 2013-06-06 15:39:03Z gerhardus $
 Versioning: a.b.c  a is a major release, b represents changes or new additions, c is a bug fix
 @version 1.0.0   29/09/2009    Gerhardus Muller     Script created
 @version 2.0.0   20/10/2010    Gerhardus Muller     provision for instance derived log files
 @version 2.1.0		24/05/2011		Gerhardus Muller		 ported to Mac
 @version 2.2.0		16/08/2012		Gerhardus Muller		 shutdownLogging changed to static; removed timestamp from object variables
 
 @note
 
 @todo
 
 @bug
 
 Copyright notice
 */

#ifndef __class_logger_has_been_included__
#define __class_logger_has_been_included__

#include <pthread.h>
#include "logging/loggerStream.h"
#include <time.h>

class logger : public loggerDefs
{
  public:
    // declarations
    static const int BUFLEN = 2048;                               ///< max length of the log message
    static const int PREAMBLE_LEN = 256;                          ///< max length of the log preamble
    static const int TIMESTAMP_LEN = 64;                          ///< allocated length for the timestamp in the thread local storage
    static const char* priorityText[4];

    // methods
  public:
    logger( const char* newName, eLogLevel newLevel );
    logger();
    ~logger();
    void init( const char* newName, eLogLevel newLevel );
    void log( priority thePriority, eLogLevel theLevel, const char* theStr, va_list args );
    void log( priority thePriority, eLogLevel theLevel, const char* theStr );
    void log( priority thePriority, eLogLevel theLevel, const std::string& theStr );
    void debug( eLogLevel theLevel, const char* str, ... );
    void info( eLogLevel theLevel, const char* str, ... );
    void warn( eLogLevel theLevel, const char* str, ... );
    void error( const char* str, ... );
    loggerStream debug( eLogLevel theLevel = MIDLEVEL ) { return loggerStream( this, DEBUG, theLevel );}
    loggerStream info( eLogLevel theLevel = MIDLEVEL )  { return loggerStream( this, INFO, theLevel );}
    loggerStream warn( eLogLevel theLevel = LOGALWAYS )  { return loggerStream( this, WARN, theLevel );}
    loggerStream error( eLogLevel theLevel = LOGALWAYS ) { return loggerStream( this, ERROR, theLevel );}
    loggerStream operator<<( const char* s ){ return loggerStream( this, DEBUG, defaultLogLevel, s );}
    loggerStream operator<<( priority p ) { return loggerStream( this, p, defaultLogLevel );}
    loggerStream operator<<( eLogLevel p ) { return loggerStream( this, DEBUG, p );}
    const char* generateTimestamp();
    const char* updateReference( const std::string& ref );
    const char* setTimestamp( const char* t );           ///< max TIMESTAMP_LEN bytes including the null terminator
    const char* getTimestamp();
    time_t getNow()                                       {return now;}
    void setAutoTimestamp( bool b )                       {bAutoTimestamp=b;}
    void setAddPid( bool b )                              {bPid=b;}
    void setAddThreadId( bool b )                         {bThreadId=b;}
    void setAddExecTrace( bool b )                        {bExecTrace=b;bAutoTimestamp=!b;}
    bool openLogfile( const char* filename, bool bFlushImmediatly=false, const char* owner=NULL, const char* group=NULL );
    bool instanceOpenLogfile( const char* filename, bool bFlushImmediatly=false );
    void chown( int userId, int groupId );
    void reopenLogfile( );
    void instanceReopenLogfile( );
    void closeLogfile( );
    void instanceCloseLogfile( );
    static void setLogConsole( bool bLog )                {bLogConsole=bLog;consoleHandle=stdout;}
    static void setLogStdErr( bool bLog )                 {bLogConsole=bLog;consoleHandle=stderr;}
    static logger& getInstance( const char* name, eLogLevel level );
    static logger& getInstance( const char* name );
    static logger& getInstance( );
    static void threadInit( );
    static void perThreadDelete( void* timestampBuffer );
    static void shutdownLogging( );
    static int  getStaticFd( )                              {return staticFd;}
    void setInstanceName( std::string& newName )            {instanceName=newName;}
    void setInstanceName( const char* newName )             {instanceName=newName;}
    std::string& getInstanceName( )                         {return instanceName;}

  protected:

  private:

    // properties
  public:

  protected:

  private:
    static int            staticFd;                           ///< logger file descriptor - default for the process
    static bool           bLogConsole;                        ///< true to log to the console as well
    static bool           bFlushImmediatly;                   ///< true to flush after every line
    static std::string    staticLogFileName;                  ///< logging file name - default for the process
    static std::string    logFileOwner;                       ///< logging file owner
    static std::string    logFileGroup;                       ///< logging file group
    static pthread_key_t  timestampKey;                       ///< thread local storage key for the timestamp
    static pthread_once_t initDone;                           ///< pthread local lockout on key creation
    static FILE*          consoleHandle;                      ///< points to either stdout or stderr
    static bool           bPid;                               ///< include a pid in the log string
    static bool           bThreadId;                          ///< include a threadId in the log string
    static bool           bExecTrace;                         ///< support for an execution trace following the timestamp, disables bAutoTimestamp
    static int            pid;                                ///< process pid
    int                   instanceFd;                         ///< logger file descriptor - object version
    bool                  bInstanceFlushImmediatly;
    std::string           instanceLogFileName;                ///< logging file name - object version
    time_t                now;                                ///< current time
    bool                  bAutoTimestamp;                     ///< auto generates the timestamp
    unsigned long         threadId;                           ///< thread id
//    char*                 timestamp;                          ///< logging timestamp
    std::string           instanceName;                       ///< instance name to be used for logging operations
};  // logger

#endif  // #ifndef __class_logger_has_been_included__
