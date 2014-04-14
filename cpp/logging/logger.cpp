/** @class logger
 Logging infrastructure
 
 $Id: logger.cpp 2879 2013-06-04 20:05:10Z gerhardus $
 Versioning: a.b.c  a is a major release, b represents changes or new additions, c is a bug fix
 @version 1.0.0   29/09/2009    Gerhardus Muller     Script created
 @version 2.0.0   20/10/2010    Gerhardus Muller     provision for instance derived log files
 @version 2.1.0		24/05/2011		Gerhardus Muller		 ported to Mac
 @version 2.2.0		20/01/2012		Gerhardus Muller		 resolved compiler complaints about nwritten not used
 @version 2.3.0		19/04/2012		Gerhardus Muller		 added buildno and buildtime to the reopen message
 @version 2.4.0		16/08/2012		Gerhardus Muller		 shutdownLogging removed from constructor and changed to static so it can be invoked from thread cleanup; removed timestamp from object variables
 @version 2.5.0		04/06/2013		Gerhardus Muller		 only attempt change of log file ownership if we are root
 
 @note
 
 @todo
 
 @bug
 
 Copyright notice
 */

#include <stdio.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <string.h>
#include <stdarg.h>
#include <stdlib.h>
#include <sys/uio.h>
#include <grp.h>
#include <pwd.h>

#include "src/options.h"
#include "logging/logger.h"
#include "utils/utils.h"
#include "exception/Exception.h"

int  logger::staticFd = -1;
bool logger::bFlushImmediatly = false;
logger::eLogLevel loggerDefs::defaultLogLevel = MIDLEVEL;
bool logger::bLogConsole = true;
bool logger::bPid = false;
int logger::pid = -1;
bool logger::bThreadId = false;
bool logger::bExecTrace = false;
FILE* logger::consoleHandle = stderr;
std::string logger::staticLogFileName;
std::string logger::logFileOwner;
std::string logger::logFileGroup;
const char* logger::priorityText[4] = {"ERROR","WARN ","info ","debug"};
pthread_key_t  logger::timestampKey;
pthread_once_t logger::initDone = PTHREAD_ONCE_INIT;

/**
 * constructor
 * @param name - instance name
 * @param level - default logging level
 * */
logger::logger( const char* newName, eLogLevel newLevel )
{
  instanceFd = -1;
  init( newName, newLevel );
} // logger

/**
 * default constructor - call init later on
 * **/
logger::logger( )
{
  logLevel = USEDEFAULT;
  info( LOGSELDOM, "logger::logger() this:%p defLogLevel:%d logLevel:%d instanceName: '%s', bAutoTimestamp %d, bExecTrace %d, pid %d", this, defaultLogLevel, logLevel, instanceName.c_str(), bAutoTimestamp, bExecTrace, pid );
} // logger

/**
 * init - allocate thread local storage
 * @param name - instance name
 * @param level - default logging level
 * **/
void logger::init( const char* newName, eLogLevel newLevel )
{
  // we want a timestamp variable per thread
  pthread_once( &initDone, threadInit );

  logLevel = USEDEFAULT;
  instanceName = newName;
  bAutoTimestamp = !bExecTrace;
  const char* timestamp = getTimestamp();
  if( bAutoTimestamp || (timestamp[0]=='\0') )
    generateTimestamp();
  pid = getpid();
  threadId = (unsigned long)pthread_self();
  info( LOGSELDOM, "logger::init this:%p defLogLevel:%d logLevel:%d instanceName: '%s', bAutoTimestamp %d, bExecTrace %d, pid %d", this, defaultLogLevel, logLevel, instanceName.c_str(), bAutoTimestamp, bExecTrace, pid );
} // init

/** 
 * destructor
 * */
logger::~logger()
{
//  instanceName.clear(); - hoekom sou ek dit wou doen? dit verdwyn in elk geval
//  shutdownLogging(); - only call out of thread cleanup code - use pthread_cleanup_push(logger::shutdownLogging) or similar
  instanceCloseLogfile();
} // ~logger

/**
 * init function per thread for the timestamp
 * **/
void logger::threadInit( )
{
  pthread_key_create( &timestampKey, logger::perThreadDelete );
} // threadInit

/**
 * delete function for the per thread timestamp
 * @param t
 * **/
void logger::perThreadDelete( void* t )
{
  char* timestampBuffer = (char*)t;
  delete[] timestampBuffer;
} // perThreadDelete

/**
 * pedantic freeing of the timestamp - mainly for mem leak testing
 * this function is only really important in a multi-threaded environment if threads are
 * created and destroyed
 * use pthread_cleanup_push(logger::shutdownLogging) out of the thread creation to ensure this runs 
 * on thread cleanup
 * **/
void logger::shutdownLogging( )
{
  char* timestamp = (char*)pthread_getspecific( timestampKey );
  if( timestamp != NULL )
  {
    delete[] timestamp;
    timestamp = NULL;
    pthread_setspecific( timestampKey, timestamp );
  } // if
} // shutdownLogging

/**
 * sets the timestamp to a predetermined value
 * **/
const char* logger::setTimestamp( const char* t )
{
  if( t == NULL ) t = "t==NULL";
  char* timestamp = (char*)pthread_getspecific( timestampKey );
  if( timestamp == NULL )
  {
    timestamp = new char[TIMESTAMP_LEN];
    pthread_setspecific( timestampKey, timestamp );
  } // if
  strncpy( timestamp, t, TIMESTAMP_LEN-1 );
  timestamp[TIMESTAMP_LEN-1] = '\0';
  return timestamp;
} // setTimestamp

/**
 * gets the timestamp
 * **/
const char* logger::getTimestamp( )
{
  char* timestamp = (char*)pthread_getspecific( timestampKey );
  if( timestamp == NULL )
  {
    timestamp = new char[TIMESTAMP_LEN];
    timestamp[0] = '\0';
    pthread_setspecific( timestampKey, timestamp );
  } // if
  return timestamp;
} // setTimestamp

/**
 * generates a logging timestamp
 * **/
const char* logger::generateTimestamp( )
{
  char sTime[64];
  struct tm *tmp;
  now = time( NULL );
  tmp = localtime( &now );
  int len = strftime( sTime, sizeof(sTime), "%F %T", tmp );
  if( bExecTrace )
    snprintf( &sTime[len], (64-len), " %011d", rand() );

  return setTimestamp( sTime );
} // generateTimestamp

/**
 * sets the reference part of the timestamp string
 * **/
const char* logger::updateReference( const std::string& ref )
{
  char sTime[64];
  struct tm *tmp;
  tmp = localtime( &now );
  int len = strftime( sTime, sizeof(sTime), "%F %T", tmp );
  strcat( sTime, " " );
  strncat( sTime, ref.c_str(), (64-len-1) );

  return setTimestamp( sTime );
} // updateReference

/**
 * method to open the log file - call before any logging commences
 * @param filename
 * @param bFlushNow - if true specify the O_SYNC flag
 * @param owner - if specified try and set the log file owner
 * @param group - if specified try and set the group for the log file
 * **/
bool logger::openLogfile( const char* filename, bool bFlushNow, const char* owner, const char* group )
{
  if( staticFd != -1 ) close( staticFd );
  if( strlen(filename) == 0 )
  {
    fprintf( stderr, "WARN openLogfile filename empty\n" );
    return false;
  } // if
  staticLogFileName = filename;
  bFlushImmediatly = bFlushNow;
  staticFd = open( filename, O_WRONLY | O_CREAT | O_APPEND, S_IRUSR | S_IWUSR | S_IRGRP | (bFlushImmediatly?O_SYNC:0) );
  if( staticFd == -1 )
  {
    fprintf( stderr, "WARN openLogfile - failed to open log '%s' - %s\n", filename, strerror( errno ) );
    return false;
  }
  else
  {
    info( LOGALWAYS, "openLogfile opened '%s' staticFd:%d flush:%d buildno:%s buildTime:%s", filename, staticFd, bFlushImmediatly, pOptions->buildNo.c_str(), pOptions->buildTime.c_str() );
  }

  if( (getuid()==0) && ((owner!=NULL) || (group!=NULL)) )
  {
    if( owner != NULL ) logFileOwner = owner;
    if( group != NULL ) logFileGroup = group;

    utils::setFileOwnership( staticFd, owner, group );
  } // if

  return true;
} // openLogfile
/**
 * method to open the log file - call before any logging commences
 * @param filename
 * @param bFlushImmediatly - if true specify the O_SYNC flag
 * @param owner - if specified try and set the log file owner
 * @param group - if specified try and set the group for the log file
 * **/
bool logger::instanceOpenLogfile( const char* filename, bool bFlushNow )
{
  if( instanceFd != -1 ) close( instanceFd );
  if( strlen(filename) == 0 )
  {
    fprintf( stderr, "WARN openLogfile filename empty\n" );
    return false;
  } // if
  instanceLogFileName = filename;
  bInstanceFlushImmediatly = bFlushNow;
  instanceFd = open( filename, O_WRONLY | O_CREAT | O_APPEND, S_IRUSR | S_IWUSR | S_IRGRP | (bInstanceFlushImmediatly?O_SYNC:0) );
  if( instanceFd == -1 )
  {
    warn( LOGALWAYS, "instanceOpenLogfile - failed to open log '%s' - %s\n", filename, strerror(errno) );
    fprintf( stderr, "WARN instanceOpenLogfile - failed to open log '%s' - %s\n", filename, strerror(errno) );
    return false;
  }
  else
  {
    info( LOGALWAYS, "instanceOpenLogfile opened '%s' instanceFd:%d flush:%d", filename, instanceFd, bInstanceFlushImmediatly );
  }

  if( (!logFileOwner.empty()) || (!logFileGroup.empty()) )
    utils::setFileOwnership( instanceFd, logFileOwner.c_str(), logFileGroup.c_str() );

  return true;
} // instanceOpenLogfile

/**
 * changes log file ownership to allow for log rotation when running 
 * as an unpriviledged user
 * **/
void logger::chown( int userId, int groupId )
{
  ::chown( staticLogFileName.c_str(), userId, groupId );
  if( !instanceLogFileName.empty() )
    ::chown( instanceLogFileName.c_str(), userId, groupId );
} // chown

/**
 * static method to close the log file
 * */
void logger::closeLogfile( )
{
  if( staticFd != -1 ) 
  {
    close( staticFd );
    staticFd = -1;
  } // if
} // closeLogfile
void logger::instanceCloseLogfile( )
{
  if( instanceFd != -1 ) 
  {
    close( instanceFd );
    instanceFd = -1;
  } // if
} // instanceCloseLogfile

/**
 * reopen logfile - used for log rotation
 * **/
void logger::reopenLogfile( )
{
  closeLogfile();
  openLogfile( staticLogFileName.c_str(), bFlushImmediatly, logFileOwner.c_str(), logFileGroup.c_str() );
} // reopenLogfile
/**
 * reopen logfile - used for log rotation
 * **/
void logger::instanceReopenLogfile( )
{
  if( instanceFd != -1 ) 
  {
    instanceCloseLogfile();
    instanceOpenLogfile( instanceLogFileName.c_str(), bInstanceFlushImmediatly );
  } // if
} // instanceReopenLogfile

/**
 * returns and instance of the logger
 * @param name - instance name
 * @param level - default logging level - loggerDefs::MAXLEVEL will log everything
 * **/
logger& logger::getInstance( const char* name, eLogLevel level )
{
  logger* theLog = new logger( name, level );
  return *theLog;
} // getInstance

/**
 * returns and instance of the logger
 * uses the default logging level
 * @param name - instance name
 * **/
logger& logger::getInstance( const char* name )
{
  logger* theLog = new logger( name, defaultLogLevel );
  return *theLog;
} // getInstance

/**
 * returns and instance of the logger - default constructor
 * call init before using
 * **/
logger& logger::getInstance( )
{
  logger* theLog = new logger( );
  return *theLog;
} // getInstance

/**
 * make a log entry
 * @param thePriority
 * @param theLevel
 * @param theStr
 * @param args - va_list
 * */
void logger::log( priority thePriority, eLogLevel theLevel, const char* theStr, va_list args )
{
  if( !wouldLog( theLevel ) ) return;

  char intBuffer[BUFLEN];
  vsnprintf( intBuffer, BUFLEN, theStr, args );
  intBuffer[BUFLEN-1] = '\0';
  log( thePriority, theLevel, intBuffer );
} // log

/**
 * make a log entry
 * @param thePriority
 * @param theLevel
 * @param theStr
 * */
void logger::log( priority thePriority, eLogLevel theLevel, const std::string& theStr )
{
  log( thePriority, theLevel, theStr.c_str() );
} // log

/**
 * make a log entry
 * @param thePriority
 * @param theLevel
 * @param theStr
 * */
void logger::log( priority thePriority, eLogLevel theLevel, const char* theStr )
{
  if( !wouldLog( theLevel ) ) return;
  if( bAutoTimestamp ) generateTimestamp( );
  char* timestamp = (char*)pthread_getspecific( timestampKey );
  threadId = (unsigned long)pthread_self();

  // generate the pid / threadId if required
  char strIds[64];
  strIds[0] = '\0';
  int strStart = 0;
  if( bPid )
    strStart = sprintf( strIds, "%u", pid );
  if( bThreadId )
  {
    if( bPid )
      sprintf( &strIds[strStart], " %lu", threadId );
    else
      sprintf( &strIds[strStart], "%lu", threadId );
  } // if

  // may have truncation to the console
  char intBuffer[BUFLEN+PREAMBLE_LEN];
  if( bLogConsole )
  {
    snprintf( intBuffer, BUFLEN, "[%s %s] %s %s\n", strIds, priorityText[thePriority], instanceName.c_str(), theStr );
    intBuffer[BUFLEN+PREAMBLE_LEN-1] = '\0';
    fputs( intBuffer, consoleHandle );
  } // if bLogConsole

  // do not limit the length of lines logged to the file
  // to guarantee non-interleaved writes from multiple processes the file
  // has to be openened in append mode and a single write or a writev should be used
  int fd = staticFd;
  if( instanceFd != -1 ) fd = instanceFd;
  if( fd > -1 )
  {
    int numBytes = snprintf( intBuffer, BUFLEN, "[%s %s %s] %s ", timestamp, strIds, priorityText[thePriority], instanceName.c_str() );
    intBuffer[BUFLEN+PREAMBLE_LEN-1] = '\0';

    // vector write to stop log interleaving between processes
    // write( staticFd, (const void*)intBuffer, numBytes );
    // write( staticFd, (const void*)theStr, strlen( theStr ) );
    // write( staticFd, (const void*)"\n", 1 );
    struct iovec iov[3];
    // ssize_t nwritten;
    iov[0].iov_base = (void*)intBuffer;
    iov[0].iov_len = numBytes;
    iov[1].iov_base = (void*)theStr;
    iov[1].iov_len = strlen( theStr );
    iov[2].iov_base = (void*)"\n";
    iov[2].iov_len = 1;
    //nwritten = writev( fd, iov, 3 );
    writev( fd, iov, 3 );
  } // if( fd
} // log

void logger::debug( eLogLevel theLevel, const char* theStr, ... )
{
  va_list args;
  va_start( args, theStr );
  log( DEBUG, theLevel, theStr, args );
  va_end( args );
} // debug

void logger::info( eLogLevel theLevel, const char* theStr, ... )
{
  va_list args;
  va_start( args, theStr );
  log( INFO, theLevel, theStr, args );
  va_end( args );
} // info

void logger::warn( eLogLevel theLevel, const char* theStr, ... )
{
  va_list args;
  va_start( args, theStr );
  log( WARN, theLevel, theStr, args );
  va_end( args );
} // warn

void logger::error( const char* theStr, ... )
{
  va_list args;
  va_start( args, theStr );
  log( ERROR, MINLEVEL, theStr, args );
  va_end( args );
} // error

