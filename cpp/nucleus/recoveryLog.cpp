/** @class recoveryLog
 recoveryLog - interface to recovery infrastructure
 
 $Id: recoveryLog.cpp 2892 2013-06-26 15:55:45Z gerhardus $
 Versioning: a.b.c a is a major release, b represents changes or new features, c represents bug fixes. 
 @version 1.0.0		01/10/2009		Gerhardus Muller		Script created
 @version 1.1.0		26/10/2010		Gerhardus Muller		use the system logrotate script rather than generating one here
 @version 1.2.0		25/11/2011		Gerhardus Muller		better error handling for reopen
 @version 1.3.0		24/06/2013		Gerhardus Muller		splitting of reOpen into an open/close for worker::closeOpenFileHandles
 @version 1.4.0		26/06/2013		Gerhardus Muller		changing the ownership of the log file if the user is root

 @note

 @todo
 
 @bug

	Copyright Notice
 */
#include <stdlib.h>
#include <stdio.h>
#include <time.h>
#include <errno.h>
#include <string.h>
#include <sys/stat.h>
//#include <fstream>
#include <errno.h>
#include "boost/regex.hpp"
#include "src/options.h"
#include "nucleus/baseEvent.h"
#include "nucleus/recoveryLog.h"
#include "nucleus/scriptExec.h"
#include "utils/utils.h"

const char *const recoveryLog::recoveryFilebase = "recovery.log";
const char *const recoveryLog::recoveryDirbase = "recovery";
const char *const recoveryLog::logDirbase = "logs";
int recoveryLog::seq = 0;
int recoveryLog::countRecoveryLines = 0;
logger recoveryLog::staticLogger = logger( "recoveryS", loggerDefs::MIDLEVEL );
logger* recoveryLog::pStaticLogger = &recoveryLog::staticLogger;

/**
 * Construction - throws if file open fails or recovery dir does not exist (or wrong permissions)
 * @param baseDir of the server
 * @param bRotate - true to rotate before opening
 * @exception on failure to open log or recovery directory
 * **/
recoveryLog::recoveryLog( const char* baseDir, bool bRotate, const std::string& logrotatePath, const std::string& runAsUser, const std::string& logGroup, int logFilesToKeep )
  : object( "recoveryLog" )
{
  bStreamOpen = false;
  countRecoveryLines = 0;
  dispatcherFd = -1;
  recoveryDir = baseDir;
  if( recoveryDir[recoveryDir.length()-1] != '/' ) recoveryDir += "/";
  recoveryDir += recoveryDirbase;
  recoveryDir += "/";
  recoveryLogname = baseDir;
  if( recoveryLogname[recoveryLogname.length()-1] != '/' ) recoveryLogname += "/";
  logDir = recoveryLogname;
  recoveryLogname += recoveryFilebase;

  // rotate first if required
  if( bRotate )
    rotate( baseDir, logrotatePath, runAsUser, logGroup, logFilesToKeep );
  
  log.info( log.LOGALWAYS, "opening recovery log %s, recovery dir '%s'", recoveryLogname.c_str(), recoveryDir.c_str() );
  ofs.open( recoveryLogname.c_str(), std::ofstream::app );
  if( !ofs.good() )
    throw Exception( log, log.ERROR, "recoveryLog: failed to open file %s", recoveryLogname.c_str() );

  // if we can change the ownership of the recovery file - otherwise the underpriviledged workers cannot re-open it
  utils::setFileOwnership( recoveryLogname.c_str(), runAsUser.c_str(), logGroup.c_str() );

  // test the recovery dir - create a file and write to it
  std::string testName;
  testName = recoveryDir +  "testfile.txt";
  std::ofstream testStream( testName.c_str() );
  testStream << "test message to make sure the recovery directory is available\n";
  testStream.flush();
  if( !testStream.good() )
  {
    ofs.close();
    throw Exception( log, log.ERROR, "failed to create test file %s in directory %s", testName.c_str(), recoveryDir.c_str() );
  }
  testStream.close();
  log.setAutoTimestamp( false );
  bStreamOpen = true;
}	// recoveryLog

/**
 * destruction
 * **/
recoveryLog::~recoveryLog()
{
  log.info( log.LOGALWAYS, "closing recovery log" );
  ofs.close( );
}	// ~recoveryLog

/**
 * reopen - typically in response to a logrotate signal
 * @exception on failure to re-open log
 * **/
void recoveryLog::reOpen( )
{
  close();
  ofs.open( recoveryLogname.c_str(), std::ofstream::app );
  if( !ofs.good() )
    throw Exception( log, log.ERROR, "reOpen: failed to open file:'%s' - %s", recoveryLogname.c_str(), strerror(errno) );
  else
    log.info( log.LOGALWAYS, "reOpen log %s", recoveryLogname.c_str() );
  bStreamOpen = true;
} // reOpen

/**
 * spawns logrotate to rotate the recovery log.  it is up to the application to ensure 
 * all logs are re-opened
 * @param baseDir of the server
 * @param logrotatePath - path to the logrotate binary
 * @param runAsUser - user to own the log files
 * @param logGroup - group owner of the created log files
 * @param logFilesToKeep - the number of rotated files to keep
 * @exception on failure
 * **/
void recoveryLog::rotate( const char* baseDir, const std::string& logrotatePath, const std::string& runAsUser, const std::string& logGroup, int logFilesToKeep )
{
  // construct the path name
  std::string logDir;
  std::string logname = baseDir;
  if( logname[logname.size()-1] != '/' )
    logname += "/";
  logDir = logname;
  logname += "*.log";
  
  // create the logrotate command script
  std::string logrotateState = logDir + "logrotate.status";

  // spawn logrotate
  baseEvent* pEvent = new baseEvent( baseEvent::EV_BIN );
  pEvent->setScriptName( logrotatePath.c_str() );
  pEvent->setStandardResponse( false );
  pEvent->addScriptParam( "-s" ); // spec an alternate state file
  pEvent->addScriptParam( logrotateState ); // spec an alternate state file
  pEvent->addScriptParam( "-f" ); // have to force it to run now
  pEvent->addScriptParam( pOptions->logrotateScript.c_str() );
  scriptExec pExec( getpid() );
  std::string result;
  baseEvent* pResult = NULL;
  bool bSuccess = pExec.process( pEvent, result, pResult );
  delete pEvent;
  pEvent = NULL;

  if( !bSuccess || ( result.length() > 0 ) )
    pStaticLogger->warn( loggerDefs::LOGALWAYS, "rotateScript success: %d output %s", bSuccess, result.c_str() );
  if( !bSuccess )
    throw Exception( *pStaticLogger, loggerDefs::ERROR, "rotate: failed executing logrotate %s script %s", logrotatePath.c_str(), pOptions->logrotateScript.c_str() );
} // rotate

/**
 * @return pointer to a string describing the state of the recoveryLog.  The string has an unspecified lifetime and is not multi-thread safe
 * **/
std::string recoveryLog::toString( )
{
  std::ostringstream oss;
  oss << typeid(*this).name() << " fn:" << recoveryLogname;
	return oss.str();
}	// toString

/**
 * **/
void recoveryLog::writeTestLine( const char* t )
{
  if( ofs.good() )
    log.debug( log.LOGNORMAL, "writeTestLine ofs good:%s", t );
  else
    throw Exception( log, log.ERROR, "writeTestLine: ofs in a not good state" );
  ofs << t << "\n";
  ofs.flush();
  if( !ofs.good() ) throw Exception( log, log.ERROR, "writeTestLine: ofs error:%s", strerror(errno) );
} // writeTestLine

/**
 * writes the event to the recovery log - that is both the list and the logfile
 * @param theEvent to log
 * @param from - name of the object owning the event
 * @param to - name of the destination object
 * **/
void recoveryLog::writeEntry( baseEvent* theEvent, const char* error, const char* from, const char* to )
{
  if( from == NULL ) from = "unknown";
  if( to == NULL ) to = "unknown";
  if( error == NULL ) error = "unknown";

  if( !ofs.good()  )
    throw Exception( log, log.ERROR, "writeEntry: ofs in a not good state" );

  // create serialisation file for event
  char filename[1024];
  snprintf( filename, 1023, "%sr%06d_XXXXXX", recoveryDir.c_str(),seq );
  int fd = mkstemp( filename );
  int numBytesWritten = -1;

  // serialise event
  if( fd > -1 )
  {
    numBytesWritten = theEvent->serialiseToFile( fd );
    if( numBytesWritten == -1 )
      log.error( "writeEntry failed to write to the serialisation file %s err %s", filename, strerror( errno ) );
    if( ::close(fd) != 0 )
      log.error( "writeEntry failed to close file %s err %s", filename, strerror( errno ) );
    if( chmod( filename, S_IRUSR|S_IWUSR|S_IRGRP ) != 0 )
      log.error( "writeEntry chmod failed on file %s err %s", filename, strerror( errno ) );
  } // if
  else
    log.error( "writeEntry failed to create serialisation file %s err %s", filename, strerror( errno ) );
  const char* result = (numBytesWritten==-1) ? "ERR" : "SUCC";

  // use the event's log timestamp if available
  std::string tt = theEvent->getTraceTimestamp( );
  if( !tt.empty() )
    log.setTimestamp( tt.c_str() );

  // retrieve the queue
  std::string queue = theEvent->getFullDestQueue();

  // write record to recovery log
  // result, date, time in seconds, error, from, to, queue, event class, serialised event filename, log timestamp, event string
  int now = time( NULL );
  ofs << result << "," << utils::timeToString( now ) << "," << now << "," << error << "," << from << "," << to << "," << queue << "," << theEvent->typeToString() << "," << filename << "," << log.getTimestamp() << "," << theEvent->toString() << std::endl;
  ofs.flush();
  if( !ofs.good() ) throw Exception( log, log.ERROR, "writeEntry: ofs error:%s", strerror(errno) );

  // log the event
  log.info( log.LOGALWAYS, "writeEntry %s from '%s' to '%s' name '%s' queue '%s' reason '%s' fn '%s' bytes %d", result, from, to, theEvent->typeToString(), queue.c_str(), error, filename, numBytesWritten );
  countRecoveryLines++;
  seq++;
} // writeEntry

/**
 * recovers an existing log.  default log to be recovered is recoveryLogname+".1"
 * recovers all lines that were successfully serialised.  after re-processing (submitting
 * for processing) an event its serialisation file is deleted.  at the end of the processing
 * a string "done recovery on datetime" is written to the end of the recovered log
 * call recovery with a delay in the loop until it returns false
 * @param fileToRecover - pass NULL for default recovery
 * @param fd - dispatcher fd
 * **/
void recoveryLog::initRecovery( const char* fileToRecover, int fd )
{
  count = 0;
  countProcessed = 0;
  countFailed = 0;
  countIgnored = 0;
  dispatcherFd = fd;
  
  // open the log to be recovered
  std::string logfile;
  if( fileToRecover == NULL )
    logfile = recoveryLogname + ".1";
  else
    logfile = fileToRecover;
  recStream.open( logfile.c_str(), std::fstream::in | std::fstream::out );
  if( recStream.fail() )
    throw Exception( log, log.WARN, "initRecovery: failed to open file '%s'", logfile.c_str() );
  else
    log.info( log.LOGALWAYS, "initRecovery: opened file '%s' for recovery", logfile.c_str() );
  
  startTime = time( NULL );
} // initRecovery

/**
 * is called by recover for cleanup at end of recovery
 * **/
void recoveryLog::finishRecovery( )
{
  // write tailer and close
  int now = time( NULL );
  recStream.clear( );
  recStream.seekp( 0, std::ios_base::end );
  recStream << std::endl << "done recovery started " << utils::timeToString( startTime ) << " ended " << utils::timeToString( now ) << " total of " << (now-startTime) << " seconds, lines " << count << " processed " << countProcessed << " events, failed on " << countFailed << " ignored " << countIgnored << std::endl;
  recStream.close( );
  
  log.info( log.LOGALWAYS, "finishRecovery: processed %d lines, %d failures, %d ignored", count, countFailed, countIgnored );
} // finishRecovery

/**
 * intention: if the dispatcher queue becomes too long recover returns true to indicate more
 * records should be recovered.  it expects the calling party to sleep or continue
 * with other work for a while
 * reality: not implemented - not that easy to figure out the length of the dispatcher queue
 * @return true for more records and false for done
 * **/
bool recoveryLog::recover( )
{
  // loop through all entries
  while( recStream.good( ) )
  {
    std::string line;
    getline( recStream, line );
    count++;
    if( line.length() > 0 )
      processLine( line );
  } // while
  
  finishRecovery( );
  return false;
} // recover

/**
 * processes a single line from the input stream. if the event is of the type dispatchEvent
 * and its destination is the dispatcher it is serialised onto the dispatcher's queue - 
 * otherwise it is ignored
 * @param line
 * **/
void recoveryLog::processLine( std::string& line )
{
  bool bProcessed = false;
  
  // parse the line
  // SUCC,Wed Jul 26 22:05:30 2006,1153944330,ser_fail,mserver_in,mserver_out,19dispatchScriptEvent,/var/spool/mserver/recovery/r000000_IpDBXK
  //                  SUCC   date    secs   error  from   to     queue  event  path
  boost::regex reg( "^(\\w+),([^,]+),(\\d+),(\\w+),(\\w+),(\\w+),(\\w+),(\\w+),([^,]+)" );
  boost::smatch m;
  bool bParsed = boost::regex_search( line, m, reg );
  if( bParsed )
  {
    bool bSerialised = (m[1] == "SUCC");  // was the serialised successfully and has not expired or retries exceeded
    std::string param = m[3];
    int  secs = atoi( param.c_str() );    // time in seconds when the event was dumped
    std::string error = m[4];         // error that caused the event to be dumped
    std::string from = m[5];          // originator of event
    std::string to = m[6];            // destination of the event
    std::string queue = m[7];         // queue
    std::string eventType = m[8];     // event class (normally derived 
    std::string path = m[9];          // path of the serialisation file
    log.info( log.LOGMOSTLY, "processLine: ser %d secs %d err '%s' from '%s' to '%s' queue '%s' event '%s' path '%s'", bSerialised, secs, error.c_str(), from.c_str(), to.c_str(), queue.c_str(), eventType.c_str(), path.c_str() );

    if( bSerialised )
    {
      try
      {
        baseEvent* pEvent = baseEvent::unSerialiseFromFile( path.c_str() );
        if( pEvent != NULL )
        {
          if( !pEvent->isExpired() )
          {
            // adjust the readytime back to a relative value
            if( pEvent->getReadyTime() > 0 )
            {
              int offset = (int)pEvent->getReadyTime() - time(NULL);
              if( offset < 0 ) offset = 0;
              pEvent->setReadyTime( offset );
            } // if

            // serialise and re-dispatch
            if( pEvent->serialise( dispatcherFd ) == -1 )
              countFailed++;
            else
            {
              bProcessed = true;
              countProcessed++;
            }
          } // if
          else
          {
            log.info( log.LOGALWAYS, "processLine: expired %s", pEvent->toString().c_str() );
            bProcessed = true;  // delete the recovery file
            countIgnored++;
          }
        } // if
        else
        {
          log.warn( log.LOGALWAYS, "processLine: failed to unserialise %s", path.c_str() );
          countFailed++;
        } // else
      } // try
      catch( Exception e )
      {
        countFailed++;
      } // catch
    } // if
    else
      countIgnored++;
    
    if( bProcessed )
      unlink( path.c_str() );   // delete on success
    else
      log.info( log.LOGALWAYS, "processLine: RM-rm %s", path.c_str() ); // write a line to the log that makes it easy to post process delete tempfiles
  } // if
  else
  {
    if( !line.empty() && (line.compare(0,21,"done recovery started") != 0) )
      log.info( log.LOGALWAYS, "processLine: failed to parse line '%s'", line.c_str() );
  } // else
} // processLine
