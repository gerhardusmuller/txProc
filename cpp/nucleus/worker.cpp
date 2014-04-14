/** @class worker
 worker - actual worker that can execute scripts, http get requests and later send mail
 
 $Id: worker.cpp 2891 2013-06-24 10:17:25Z gerhardus $
 Versioning: a.b.c a is a major release, b represents changes or new features, c represents bug fixes. 
 @version 1.0.0		29/09/2009		Gerhardus Muller		Script created
 @version 1.1.0		11/02/2011		Gerhardus Muller		Catching of json errors in the main loop
 @version 1.2.0		30/03/2011		Gerhardus Muller		added a label for createSocketPair
 @version 1.3.0		12/04/2011		Gerhardus Muller		added support for return objects from persistent objects and removed appending the log timestamp to the trace
 @version 1.4.0		21/08/2012		Gerhardus Muller		propagated dynamic execTimeLimit; added an info event for persistent apps
 @version 1.5.0		30/08/2012		Gerhardus Muller		made provision for a default url, default script and queue management events
 @version 1.5.1		22/09/2012		Gerhardus Muller		fixed return routing for events not originating from a persistent app
 @version 1.5.2		25/02/2013		Gerhardus Muller		return event serialised without checking its return value
 @version 1.6.0		05/06/2013		Gerhardus Muller		support for FD_CLOEXEC
 @version 1.7.0		20/06/2013		Gerhardus Muller		support for the fdsToRemainOpen list and reopening the recoveryLog

 @note

 @todo
 
 @bug

	Copyright Notice
 */

#include <signal.h>
#include <errno.h>
#include <string.h>
#include <sys/types.h> 
#include <unistd.h>
#include <sys/resource.h>
#include <dirent.h>
#include "boost/regex.hpp"
#include "boost/tokenizer.hpp"
#include "src/options.h"
#include "nucleus/worker.h"
#include "nucleus/workerDescriptor.h"
#include "nucleus/urlRequest.h"
#include "nucleus/scriptExec.h"
#include "nucleus/recoveryLog.h"
#include "nucleus/queueManagementEvent.h"
#include "utils/utils.h"

bool worker::bRunning = true;
int worker::pid = 0;
int worker::signalFd0 = 0;
const char *const worker::FROM = typeid( worker ).name();
const char *const worker::TO_WORKER_DESCRIPTOR = typeid( workerDescriptor ).name();
bool worker::bReopenLog = false;
bool worker::bResetStats = false;
bool worker::bDump = false;
scriptExec* worker::pScriptExec = NULL;
recoveryLog* worker::theRecoveryLog = NULL;

/**
 Construction
 */
worker::worker( )
  : object( "worker" )
{
  pRecSock = NULL;
  pid = 0;
  pUrlRequest = NULL;
  pScriptExec = NULL;
  bRecoveryProcess = false;
  theRecoveryLog = NULL;
  pQueueManagement = NULL;
  bWroteRecovery = false;
  nucleusFd = -1;
  log.setAddPid( true );
  elapsedTime = 0;
  bPersistentApp = false;
  bExitWhenDone = false;
  maxTimeToRun = 0;
  numHits = 0;
}	// worker

worker::worker( tQueueDescriptor* theQueueDescriptor, int theFd, recoveryLog* recovery, bool bRecovery, int theMainFd )
  : object( "worker" ),
    pContainerDesc( theQueueDescriptor ),
    bRecoveryProcess( bRecovery ),
    fd( theFd ),
    nucleusFd( theMainFd )
{
  queueName = pContainerDesc->name;
  persistentApp = pContainerDesc->persistentApp;
  theRecoveryLog = recovery;

  // prevent file handles from being available / open on all the children
  closeOpenFileHandles();
  
  // use a timeout shorter than the timeout for the process to allow libcurl the 
  // opportunity to rather timeout
  maxTimeToRun = pContainerDesc->maxExecTime - URL_MAX_TIME_OFFSET;
  if( maxTimeToRun <= 0 ) maxTimeToRun = pContainerDesc->maxExecTime;
  std::string logfile = pOptions->logBaseDir;
  logfile.append( "q_" );
  logfile.append( pContainerDesc->name );
  logfile.append( ".log" );
  log.info( log.LOGALWAYS, "worker: queue:%s changing logging to '%s'", pContainerDesc->name.c_str(), logfile.c_str() );
  log.closeLogfile();
  log.openLogfile( logfile.c_str(), pOptionsNucleus->bFlushLogs );
  log.setAddExecTrace( true );
  log.setAddPid( true );
  char name[64];
  sprintf( name, "worker%d-%s", fd, queueName.c_str() );
  log.setInstanceName( name );

  unixSocket::createSocketPair( signalFd, name );
  signalFd0 = signalFd[0];
  pSignalSock = new unixSocket( signalFd[1], unixSocket::ET_SIGNAL, false, "signalFd1" );
  pSignalSock->setNonblocking( );
  sprintf( name, "workerFd%d", fd );
  pRecSock = new unixSocket( fd, unixSocket::ET_QUEUE_EVENT, false, name );
  pRecSock->setNonblocking( );
  pRecSock->initPoll( 2 );
  pRecSock->addReadFd( fd );
  pRecSock->addReadFd( signalFd[1] );

  pUrlRequest = NULL;
  pScriptExec = NULL;
  pQueueManagement = NULL;
  bWroteRecovery = false;
  elapsedTime = 0;
  bPersistentApp = false;
  bExitWhenDone = false;
  numHits = 0;

  if( !pContainerDesc->defaultScript.empty() )
  {
    defaultScript = pContainerDesc->defaultScript;
    log.info( log.LOGMOSTLY, "worker defaultScript:%s", defaultScript.c_str() );
  } // if
  if( !pContainerDesc->defaultUrl.empty() )
  {
    defaultUrl = pContainerDesc->defaultUrl;
    log.info( log.LOGMOSTLY, "worker defaultUrl:%s", defaultUrl.c_str() );
  } // if
} // worker

/**
 Destruction
 */
worker::~worker()
{
  log.generateTimestamp();
  if( bPersistentApp && (pScriptExec != NULL) )
  {
    if( pScriptExec->getChildPid() > 0 )
    {
      log.warn( log.LOGALWAYS, "~worker: killing persistent child pid %d to exit", pScriptExec->getChildPid() );
      kill( pScriptExec->getChildPid(), SIGKILL );
      pScriptExec->waitForChildExit();
    } // if( childPid
  } // if( bPersistentApp
  if( pSignalSock != NULL ) delete pSignalSock;
  if( pRecSock != NULL ) delete pRecSock;
  if( pUrlRequest != NULL ) delete pUrlRequest;
  if( pScriptExec != NULL ) delete pScriptExec;
  if( pQueueManagement != NULL ) delete pQueueManagement;
}	// ~worker

/**
 * send a result message - it is not regarded as an error if there is no returnFd specified
 * do not send result messages if it is a recovery process - the fd in the event is not valid
 * if a result object is given use that - the return path if specified via returnFd takes precedence
 * if a result object exists but no returnFd it is given to the nucleus to handle and should have 
 * its queue configured properly
 *
 * @param pEvent - the source event
 * @param bSuccess - result of the execution
 * @param result - string result
 * @param pResult - result object if it exists
 * **/
void worker::sendResult( baseEvent* pEvent, bool bSuccess, const std::string& result, const std::string& errorString, const std::string& traceTimestamp, const std::string& failureCause, const std::string& systemParam, baseEvent* pResult )
{
  int returnFd = pEvent->getReturnFd( );

  // returnFd is not valid in a recovery process
  // not sure that I agree about the expired process - will have to check the flow when it is 
  // expired - if a process is waiting it should get an answer
  if( (returnFd!=-1) && (bRecoveryProcess || pEvent->hasBeenExpired()) ) return;

  // if we have a valid returnFd send either the result object or a constructed result back
  if( returnFd != -1 )
  {
    pEvent->shiftReturnFd();  // drop the return fd that we are using

    if( pResult != NULL )
    {
      pResult->setReturnFd( pEvent->getFullReturnFd() );
      pResult->serialise( returnFd );
      if( log.wouldLog(log.MIDLEVEL) ) log.info( log.MIDLEVEL ) << "sendResult 1: fd:" << returnFd << " :" << pResult->toString();
    } // if
    else
    {
      baseEvent* pReturn = new baseEvent( baseEvent::EV_RESULT );
      pReturn->setSuccess( bSuccess );
      if( !result.empty() ) pReturn->setResult( result );
      pReturn->setRef( pEvent->getRef() );
      if( !errorString.empty() ) pReturn->setErrorString( errorString );
      if( !traceTimestamp.empty() ) pReturn->setTraceTimestamp( traceTimestamp );
      if( !failureCause.empty() ) pReturn->setFailureCause( failureCause );
      if( !systemParam.empty() ) pReturn->setSystemParam( systemParam );
      pReturn->setReturnFd( pEvent->getFullReturnFd() );

      // parse the return parameters - assume name:value\n format
      // add these to the result object
      if( pEvent->getStandardResponse( ) )
      {
        std::string regStr = "(\\w+):([^\\n]*)";
        boost::regex reg( regStr );
        boost::smatch m;
        std::string::const_iterator it = result.begin();
        std::string::const_iterator end = result.end();
        while( boost::regex_search( it, end, m, reg ) )
        {
          std::string name;
          std::string value;
          name = m[1];
          value = m[2];
          pReturn->addParam( name.c_str(), value );
          it = m[0].second;
        } // while
      } // if( getStandardResponse

      pReturn->serialise( returnFd );
      log.info( log.MIDLEVEL ) << "sendResult: fd:" << returnFd << " :" << pReturn->toString();
      delete pReturn;
    } // else( pResult != NULL ) 
  } // if( returnFd != -1 ) 
  else
  {
    // for this case we do not have a returnFd but we have a result object - submit this to the
    // nucleus for queueing and execution only if the destQueue is setup
    if( (pResult != NULL) && (!pResult->getFullDestQueue().empty()))
    {
      pResult->serialise( nucleusFd );
      if( log.wouldLog(log.MIDLEVEL) ) log.info( log.MIDLEVEL ) << "sendResult 2:" << pResult->toString();
    } // if
  } // else
} // sendResult

/**
 * sends a done message back to the parent
 * **/
void worker::sendDone( )
{
  baseEvent done( baseEvent::EV_WORKER_DONE );
  done.setElapsedTime( elapsedTime );
  done.setRecoveryEvent( bWroteRecovery );
  done.serialise( fd );
  log.debug( log.LOGNORMAL, "sendDone:'%s' fd:%d", done.toString().c_str(), fd );
} // sendDone

/**
 * either creates a return EV_ERROR object or writes an entry to the 
 * recovery log if the event failed and its retries have not been exceeded
 * @param pEvent - the source event
 * @param bSuccess - result of the execution
 * @param error
 * **/
void worker::logForRecovery( baseEvent* pEvent, bool bSuccess, const std::string& error )
{
  if( !bSuccess )
  {
    if( !pContainerDesc->errorQueue.empty() )
    {
      // change the type to EV_ERROR and give back to nucleus
      pEvent->setType( baseEvent::EV_ERROR );
      pEvent->setDestQueue( pContainerDesc->errorQueue );
      pEvent->setErrorString( error );
      pEvent->serialise( nucleusFd );
      log.info( log.LOGMOSTLY ) << "logForRecovery:returning: " << pEvent->toString();
    } // if
    else
    {
      bWroteRecovery = true;
      if( !pEvent->isRetryExceeded() )
      {
        pEvent->incRetryCounter();
        if( theRecoveryLog != NULL )
          theRecoveryLog->writeEntry( pEvent, "exec_fail", FROM, FROM );
        else
          log.warn( log.LOGALWAYS ) << "logForRecovery: (theRecoveryLog is NULL) failed for " << pEvent->toString();
      } // if
      else
      {
        log.info( log.LOGALWAYS ) << "logForRecovery: retries exceeded dumping event " << pEvent->toString();
      } // else
    } // else
  } // if
} // logForRecovery

/**
 * main loop of the worker
 * **/
void worker::main( )
{
  // register signal handling
  signal( SIGINT, SIG_IGN );
  signal( SIGTERM, worker::sigHandler );
  signal( SIGCHLD, worker::sigHandler );
  pid = getpid();
  baseEvent* pEvent = NULL;
  pUrlRequest = new urlRequest( pid, pOptionsNucleus->urlSuccess, pOptionsNucleus->urlFailure, pOptionsNucleus->errorPrefix, pOptionsNucleus->tracePrefix, pOptionsNucleus->paramPrefix, maxTimeToRun, queueName, (pContainerDesc->parseResponseForObject==1), pContainerDesc->defaultUrl );
  pScriptExec = new scriptExec( pid, pOptionsNucleus->perlPath, pOptionsNucleus->shellPath, pOptionsNucleus->execSuccess, pOptionsNucleus->execFailure, pOptionsNucleus->errorPrefix, pOptionsNucleus->tracePrefix, pOptionsNucleus->paramPrefix, queueName, (pContainerDesc->parseResponseForObject==1), pContainerDesc->defaultScript );
  pQueueManagement = new queueManagementEvent( pUrlRequest, pScriptExec, pContainerDesc, nucleusFd );
  pUrlRequest->setManagementObj( pQueueManagement );
  pScriptExec->setManagementObj( pQueueManagement );
  if( pOptionsNucleus->rlimitAs > 0 ) pScriptExec->setResourceLimit( RLIMIT_AS, pOptionsNucleus->rlimitAs );
  if( pOptionsNucleus->rlimitCpu > 0 ) pScriptExec->setResourceLimit( RLIMIT_CPU, pOptionsNucleus->rlimitCpu );
  if( pOptionsNucleus->rlimitData > 0 ) pScriptExec->setResourceLimit( RLIMIT_DATA, pOptionsNucleus->rlimitData );
  if( pOptionsNucleus->rlimitFsize > 0 ) pScriptExec->setResourceLimit( RLIMIT_DATA, pOptionsNucleus->rlimitFsize );
  if( pOptionsNucleus->rlimitStack > 0 ) pScriptExec->setResourceLimit( RLIMIT_STACK, pOptionsNucleus->rlimitStack );

  // configure logging
  log.setDefaultLevel( (loggerDefs::eLogLevel) pOptionsNucleus->defaultLogLevel );
  if( pOptionsNucleus->bLogStderr )
    log.setLogStdErr( pOptionsNucleus->bLogStderr );
  else if( pOptionsNucleus->bLogConsole )
    log.setLogConsole( pOptionsNucleus->bLogConsole );
  log.setAddPid( true );
  log.setAddExecTrace( true );
  log.generateTimestamp();
  unixSocket::pStaticLogger->init( "unixSocket", (loggerDefs::eLogLevel)pOptionsNucleus->defaultLogLevel );
  unixSocket::pStaticLogger->setAddPid( true );

  // only allow the spawned scripts to regain root priviledges if we are configured to do so
  if( pContainerDesc->bRunPriviledged )
  {
    log.warn( log.LOGALWAYS, "main: priviledges can be restored" );
  } // if
  else
  {
    int retval = utils::dropPriviledgePerm();
    if( retval == 0 )
      log.info( log.LOGALWAYS, "main: dropping all priviledges" );
    else
      log.warn( log.LOGALWAYS, "main: failed to permanently drop priviledges" );
  } // else

  // check if we are required to run a persistent app
  if( !persistentApp.empty() )
  {
    // parse the command line into a command and arguments - parameters are space separated and can be quoted with '"' 
    // '\' is the escape character for an embedded '"'
    int i = 0;
    std::string appBase;
    boost::escaped_list_separator<char> sepFunc( '\\', ' ', '\"' );
    boost::tokenizer<boost::escaped_list_separator<char> > tok( persistentApp, sepFunc );
    for( boost::tokenizer<boost::escaped_list_separator<char> >::iterator beg=tok.begin(); beg!=tok.end(); ++beg)
    {
      if( i == 0 )
      {
        appBase = *beg;
      } // if
      else
      {
        persistentScript.addScriptParam( *beg );
        persistentParams.append( "'" );
        persistentParams.append( *beg );
        persistentParams.append( "' " );
      } // else
      i++;
    } // for
    persistentApp = appBase;

    // guess the type of script - a .pl or .sh extension before the first whitespace in the command
    baseEvent::eEventType persistentExecType = baseEvent::EV_BIN;
    std::string regStr = "^\\S+(\\.pl|\\.sh)";
    boost::regex reg( regStr );
    boost::smatch m;
    std::string::const_iterator it = persistentApp.begin();
    std::string::const_iterator end = persistentApp.end();
    bool bRetval = boost::regex_search( it, end, m, reg );
    if( bRetval )
    {
      std::string extension = m[1];
      if( extension.compare(".pl") == 0 ) persistentExecType = baseEvent::EV_PERL;
      else if( extension.compare(".pl") == 0 ) persistentExecType = baseEvent::EV_SCRIPT;
    } // if

    persistentScript.setType( persistentExecType );
    persistentScript.setScriptName( persistentApp );
    log.info( log.LOGMOSTLY, "main: command:'%s' %s", persistentApp.c_str(), persistentParams.c_str() );
    bPersistentApp = true;

    try
    {
      pScriptExec->spawnScript( &persistentScript, true );
    } // try
    catch( Exception e )
    {
      sleep(1); // dont flood the system with respawns
      return;   // no sense to continue if we cannot start up
    } // catch
  } // if

  // in the main loop we either spawn new script/curl events or we are in persistent
  // mode and simply pass the requests down to the application via its stdin
  bRunning = true;
  while( bRunning )
  {
    try
    {
      bWroteRecovery = false;
      bool bReady = false;
      try
      {
        bReady = pRecSock->multiFdWaitForEvent();
      }
      catch( Exception e )
      {
        log.warn( log.LOGALWAYS, "main: exception from waitForEvent" );
        bRunning = false;   // fatal error
        throw;
      } // catch

      if( bReady )
      {
        int newFd = 0;
        // retrieve the available file descriptors and process the events
        while( ( newFd = pRecSock->getNextFd( ) ) > 0 )
        {
          if( newFd == fd )
          {
            numHits++;
            log.debug( log.LOGONOCCASION, "main: about to unserialise (%u hits)", numHits );
            pEvent = baseEvent::unSerialise( pRecSock );
            if( pEvent != NULL ) 
            {
              std::string traceTS = pEvent->getTraceTimestamp();
              if( !traceTS.empty() ) log.setTimestamp( traceTS.c_str() );

              if( pEvent->getType() == baseEvent::EV_COMMAND )
              {
                if( log.wouldLog( log.LOGNORMAL ) )
                  log.info( log.MIDLEVEL ) << "main: received event:" << pEvent->toString();
                else
                  log.info( log.MIDLEVEL ) << "main: received event:" << pEvent->typeToString() << " cmd:" << pEvent->commandToString();

                // execute command requested
                if( pEvent->getCommand() == baseEvent::CMD_SHUTDOWN )
                {
                  log.info( log.LOGALWAYS, "main: CMD_SHUTDOWN" );
                  if( bPersistentApp )
                  {
                    kill( pScriptExec->getChildPid(), SIGTERM );
                    pScriptExec->waitForChildExit();
                  } // if
                  bRunning = false;
                } // if
                else if( pEvent->getCommand() == baseEvent::CMD_WORKER_CONF )
                {
                  reconfigure( pEvent );
                } // if
                else
                {
                  // send all command events to the persistent app as well
                  if( bPersistentApp )
                  {
                    if( pEvent->getCommand() == baseEvent::CMD_EXIT_WHEN_DONE ) bExitWhenDone = true;
                    process( pEvent );
                  } // if
                  if( pEvent->getCommand() == baseEvent::CMD_STATS ) 
                  {
                    std::string time = pEvent->getParam( "time" );
                    if( time.empty() ) time = utils::timeToString( ::time(NULL) );
                    dumpHttp( time );
                  } // if
                  else if( pEvent->getCommand() == baseEvent::CMD_RESET_STATS )
                  {
                    theRecoveryLog->resetCountRecoveryLines();
                    numHits = 0;
                  } // else if
                  else if( pEvent->getCommand() == baseEvent::CMD_REOPEN_LOG )
                  {
                    log.debug( log.MIDLEVEL, "main: CMD_REOPEN_LOG" );
                    log.reopenLogfile();
                    theRecoveryLog->reOpen();
                  } // if
                  else if( pEvent->getCommand() == baseEvent::CMD_END_OF_QUEUE )
                  {
                    log.debug( log.MIDLEVEL, "main: CMD_END_OF_QUEUE" );
                    sendDone();
                  } // if
                } // else
              } // if EV_COMMAND
              else
              {
                if( log.wouldLog( log.LOGONOCCASION ) )
                  log.info( log.MIDLEVEL ) << "main: received event1:" << pEvent->toString();
                else
                  log.info( log.MIDLEVEL ) << "main: received event1:" << pEvent->toStringBrief();

                // process the event
                if( !pEvent->isExpired() )
                {
                  process( pEvent );

                  // record the finish time for log analysis - log timestamp carries the original timestamp
                  char sTime[64];
                  struct tm *tmp;
                  time_t now = time( NULL );
                  tmp = localtime( &now );
                  strftime( sTime, sizeof(sTime), "%F %T", tmp );
                  log.debug( log.MIDLEVEL, "main: done processing event %s elapsed time %d done timestamp %s", pEvent->typeToString(), elapsedTime, sTime );
                } // if
                else
                {
                  // set the failureCause to expired
                  sendResult( pEvent, false, std::string(), std::string(), std::string(), std::string("expired") );
                  log.warn( log.LOGALWAYS ) << "main: expired " << pEvent->toString();
                } // else

                // indicate we are done - we don't send done events for commands - the worker is not first removed from the idle queue
                sendDone();
              } // else

              delete pEvent;
              pEvent = NULL;
            } // if( pEvent != NULL
          } // if( newFd == eventSouceFd )
          else if( newFd == signalFd[1] )  // service a signal event
          {
            baseEvent::eCommandType theCommand = baseEvent::CMD_NONE;
            int bytesReceived = pSignalSock->read( (char*)(&theCommand), sizeof(theCommand) );
            if( (bytesReceived==sizeof(theCommand)) && (theCommand==baseEvent::CMD_CHILD_SIGNAL) )
            {
              if( bPersistentApp )
              {
                pScriptExec->waitForChildExit();
                if( bExitWhenDone )
                {
                  bRunning = false;
                  log.warn( log.LOGALWAYS, "main: persistent app exited - bExitWhenDone" );
                } // if
                else if( bRunning )
                {
                  log.warn( log.LOGALWAYS, "main: persistent app exited - restarting command:'%s' %s", persistentApp.c_str(), persistentParams.c_str() );
                  // this is a bit messy - it is not that easy to decide if we were busy when we died.  for the moment
                  // if we are killed by a SIGTERM (default kill action) assume that the process was killed by hand and don't send
                  // a sendDone
                  // if( pScriptExec->getTermSignal() != SIGTERM )
                  // this problem is handled in workerPool - it checks if the worker was busy in its
                  // reconing and only pushes it onto the idle list if it was busy
                  sendDone(); // its a reasonable assumption that the child was busy when dying
                  pQueueManagement->genEvent( queueManagementEvent::QMAN_PDIED );
                  try
                  {
                    // block SIG_CHLD - if the script exits immediately it kills the sleep
                    sigset_t blockmask;
                    sigset_t oldmask;
                    sigemptyset( &blockmask );    // unblock all the signals
                    sigaddset( &blockmask, SIGCHLD );
                    if( sigprocmask( SIG_SETMASK, &blockmask, &oldmask ) < 0 )
                      throw Exception( log, log.ERROR, "main: sigprocmask returned -1" );

                    pScriptExec->spawnScript( &persistentScript, true );
                    sleep(pOptionsNucleus->persistentAppRespawnDelay); // dont flood the system with respawns
                    if( sigprocmask( SIG_SETMASK, &oldmask, &blockmask ) < 0 )
                      throw Exception( log, log.ERROR, "main: sigprocmask1 returned -1" );
                  } // try
                  catch( Exception e )
                  {
                    sleep(pOptionsNucleus->persistentAppRespawnDelay); // dont flood the system with respawns
                    return;   // no sense to continue if we cannot start up
                  } // catch
                } // if( bRunning
                else
                  log.warn( log.LOGALWAYS, "main: persistent app exited - not restarting" );
              } // if( bPersistentApp
            } // if
            else
              log.warn( log.LOGALWAYS, "main: signalFd[1] not recognising command:%d", (int)theCommand );
          } // if( fd == signalFd[1]
          else
            log.warn( log.LOGALWAYS, "main: fd:%d not handled", newFd );
        } // while fd = pRecSock->getNextFd
      } // if( bReady
      else
        log.warn( log.LOGALWAYS, "main: waitForEvent returned with no fd's available" );
    } // try
    catch( Exception e )
    {
      if( pEvent != NULL )
      {
        log.error() << "main: caught exception:" << e.getMessage() << " event:" << pEvent->toString();
        std::string mess( "exception: " );
        mess += e.getMessage();
        sendResult( pEvent, false, mess );
        if( pEvent->getType() != baseEvent::EV_COMMAND ) sendDone();
        delete pEvent;
        pEvent = NULL;
      } // if
      else
        log.error() << "main: caught exception:" << e.getMessage() << " event:NULL";
      if( elapsedTime == 0 ) elapsedTime = time(NULL) - timeStarted;
    } // catch
    catch( std::runtime_error e )
    { // json-cpp throws runtime_error
      if( pEvent != NULL )
      {
        log.error() << "main: caught std::runtime_error:" << e.what() << " event:" << pEvent->toString();
        std::string mess( "exception: " );
        mess += e.what();
        sendResult( pEvent, false, mess );
        if( pEvent->getType() != baseEvent::EV_COMMAND ) sendDone();
        delete pEvent;
        pEvent = NULL;
      } // if
      else
        log.error( "main: caught std::runtime_error:'%s'", e.what() );
    } // catch
    catch ( cURLpp::LogicError& e ) 
    {
      if( pEvent != NULL )
      {
        log.error() << "main: caught cURLpp::LogicError:" << e.what() << " event:" << pEvent->toString();
        std::string mess( "exception: " );
        mess += e.what();
        sendResult( pEvent, false, mess );
        if( pEvent->getType() != baseEvent::EV_COMMAND ) sendDone();
        delete pEvent;
        pEvent = NULL;
      } // if
      else
        log.error( "main failed: %s", e.what() );
    }
    catch( ... )
    {
      if( pEvent != NULL )
      {
        log.error() << "main: caught unknown exception:" << " event:" << pEvent->toString();
        std::string mess( "exception: " );
        sendResult( pEvent, false, mess );
        if( pEvent->getType() != baseEvent::EV_COMMAND ) sendDone();
        delete pEvent;
        pEvent = NULL;
      } // if
      else
        log.error( "main failed: caught unknown exception" );
    } // catch
  } // while  

  log.info( log.LOGALWAYS, "main: exited" );
} // main

/**
 * handles a reconfigure command CMD_WORKER_CONF 
 * @param pCommand
 * @exception on error
 * **/
void worker::reconfigure( baseEvent* pCommand )
{
  try
  {
    log.info( log.LOGMOSTLY ) << "reconfigure: received command '" << pCommand->toString() << "'";
    std::string cmd = pCommand->getParam( "cmd" );
    if( (cmd.length()==0) )
      throw Exception( log, log.WARN, "reconfigure: require parameter cmd" );

    if( cmd.compare( "setlimit" ) == 0 )
    {
      std::string resource = pCommand->getParam( "resource" );
      std::string val = pCommand->getParam( "val" );
      if( (resource.length()==0)||(val.length()==0) )
        throw Exception( log, log.WARN, "reconfigure: require parameters resource,val for command '%s'", cmd.c_str() );

      unsigned long limit = strtoul( val.c_str(), NULL, 10 );
      pScriptExec->setResourceLimit( resource, limit );
    } // if( cmd.compare
    else if( cmd.compare( "updatemaxexectime" ) == 0 )
    {
      std::string val = pCommand->getParam( "val" );
      if( val.length()==0 )
        throw Exception( log, log.WARN, "reconfigure: require parameters val for command '%s'", cmd.c_str() );
      pContainerDesc->maxExecTime = atoi( val.c_str() );
      maxTimeToRun = pContainerDesc->maxExecTime - URL_MAX_TIME_OFFSET;
      if( maxTimeToRun <= 0 ) maxTimeToRun = pContainerDesc->maxExecTime;
      pUrlRequest->setMaxTimeToRun( maxTimeToRun );
    } // if( cmd.compare
    else
      log.warn( log.LOGALWAYS, "reconfigure: cmd '$s' not supported", cmd.c_str() );
  } // try
  catch( Exception e )
  {
  } // catch
} // reconfigure

/**
 * execute the requested script / persistent invocation
 * @param pEvent - the event to execute
 * @exception amongst others on persistent child exiting
 * **/
void worker::process( baseEvent* pEvent )
{
  elapsedTime = 0;
  timeStarted = time( NULL );

  if( bPersistentApp )
  {
    baseEvent* pReturn = pScriptExec->readWritePipe( pEvent );

    if( pReturn != NULL )
    {
      // if the return type is EV_RESULT we can derive the success of the operation
      if( pReturn->getType() == baseEvent::EV_RESULT )
      {
        bool bSuccess = pReturn->isSuccess();
        logForRecovery( pEvent, bSuccess, pScriptExec->getFailureCause() );
      } // if

      int returnFd = pEvent->getReturnFd();
      if( (returnFd!= -1) && !bRecoveryProcess )
      {
        pEvent->shiftReturnFd();  // drop the return fd that we have just used
        pReturn->setReturnFd( pEvent->getFullReturnFd() );
        int retVal = pReturn->serialise( returnFd );
        if( retVal > -1 )
          log.info( log.LOGNORMAL ) << "sendResult to fd:" << returnFd << " bytes:" << retVal << " - " << pReturn->toString();
        else
          log.warn( log.LOGMOSTLY ) << "sendResult failed to fd:" << returnFd << " err:" << strerror(errno) << " - " << pReturn->toString();
      } // if
      else
      {
        if( !pReturn->getFullDestQueue().empty())
        {
          pReturn->serialise( nucleusFd );
          if( log.wouldLog(log.MIDLEVEL) ) log.info( log.MIDLEVEL ) << "process:" << pReturn->toString();
        } // if
      } // if

      delete pReturn;
    } // if pReturn
  } // if( bPersistentApp
  else
  {
    if( (pEvent->getType()==baseEvent::EV_PERL)||(pEvent->getType()==baseEvent::EV_SCRIPT)||(pEvent->getType()==baseEvent::EV_BIN) )
    {
      std::string result;
      baseEvent* pResult = NULL;
      bool bSuccess = false;
      try
      {
        bSuccess = pScriptExec->process( pEvent, result, pResult );
      } // try
      catch( Exception e )
      {
        pScriptExec->setFailureCause( e.getMessage() );
      } // catch
      sendResult( pEvent, bSuccess, result, pScriptExec->getErrorString(), pScriptExec->getTraceTimestamp(), pScriptExec->getFailureCause(), pScriptExec->getSystemParam(), pResult );
      logForRecovery( pEvent, bSuccess, pScriptExec->getFailureCause() );
      if( pResult != NULL ) delete pResult;
    }
    else if( pEvent->getType() == baseEvent::EV_URL )
    {
      std::string result;
      baseEvent* pResult = NULL;
      bool bSuccess = false;
      try
      {
        bSuccess = pUrlRequest->process( pEvent, result, pResult );
      } // try
      catch( Exception e )
      {
        pUrlRequest->setFailureCause( e.getMessage() );
      } // catch
      sendResult( pEvent, bSuccess, result, pUrlRequest->getErrorString(), pUrlRequest->getTraceTimestamp( ), pUrlRequest->getFailureCause(), pUrlRequest->getSystemParam(), pResult );
      logForRecovery( pEvent, bSuccess, pUrlRequest->getFailureCause() );
      if( pResult != NULL ) delete pResult;
    }
    else if( pEvent->getType() == baseEvent::EV_COMMAND )
    {
      log.warn( log.LOGALWAYS ) << "process: unable to handle command event: " << pEvent->toString();
    }
    else 
    {
      log.warn( log.LOGALWAYS ) << "process unable to handle event: " << pEvent->toString();
    }
  } // else( bPersistentApp

  elapsedTime = time(NULL) - timeStarted;
} // process
    
/**
 * close the unwanted file handles (sockets and files)
 * **/
void worker::closeOpenFileHandles( )
{
  DIR *dir;
  struct dirent *ent;
  std::string closedFiles;
  std::string skippedFiles;

  theRecoveryLog->close();
  if( (dir=opendir("/proc/self/fd/")) != NULL )
  {
    int dirFd = dirfd( dir );
    int loggingFd = log.getStaticFd();

    // print all the files and directories within directory
    while( (ent=readdir(dir)) != NULL)
    {
      // we need to exclude a number of file descriptors
      int openFd = atoi( ent->d_name );

      // some more exclusions
      bool bExclude = false;
      for( unsigned i = 0; !bExclude && (i<pContainerDesc->fdsToRemainOpen.size()); i++ )
        if( openFd == pContainerDesc->fdsToRemainOpen[i] ) bExclude = true;

      if( 
          !bExclude &&
          ( openFd > 2 ) &&           // stdin/out/err and the entries . ..
          ( openFd != nucleusFd ) &&  // socket back to the nucleus for life cycle events and return objects
          ( openFd != fd ) &&         // our own socket on which we receive instructions
          ( openFd != dirFd ) &&      // /proc/self/fd/ directory file handle
          ( openFd != loggingFd )     // dont rip our logging carpet from us
        )
      {
        closedFiles.append( ent->d_name );
        closedFiles.append( "," );
        close( openFd );
      } // if
      else
      {
        skippedFiles.append( ent->d_name );
        skippedFiles.append( "," );
      } // else
    } // while

    log.debug( log.LOGONOCCASION, "closeOpenFileHandles skipped:%s closed:%s", skippedFiles.c_str(), closedFiles.c_str() );
    closedir( dir );
  } // if
  else
  {
    // could not open directory
    log.warn( log.LOGALWAYS, "closeOpenFileHandles failed to open /proc/self/fd:%s", strerror(errno) );
  } // else

  theRecoveryLog->reOpen();
} // closeOpenFileHandles

/**
 Standard logging call - produces a generic text version of the worker.
 Memory allocation / deleting is handled by this worker.
 @return pointer to a string describing the state of the worker.  The string has an unspecified lifetime and is not multi-thread safe
 */
std::string worker::toString( )
{
  std::ostringstream oss;
  oss << typeid(*this).name() << ":" << this << " fd " << fd << " pid " << pid;
	return oss.str();
}	// toString

/**
 * dump as http - url reporting
 * **/
void worker::dumpHttp( const std::string& time )
{
  int count = theRecoveryLog->getCountRecoveryLines();
  if( ( pOptionsNucleus->statsUrl.length() > 0 ) && ( count > 0 ) )
  {
    baseEvent* pEvent = new baseEvent( baseEvent::EV_URL, pOptionsNucleus->statsQueue.c_str() );
    pEvent->setUrl( pOptionsNucleus->statsUrl );
    pEvent->addParam( "name", typeid(*this).name() );
    pEvent->addParam( "time", time );
    pEvent->addParam( "pid", pid );
    pEvent->addParam( "wRecoveryCount", count );
    pEvent->addParam( "numHits", numHits );
    if( bPersistentApp ) pEvent->addParam( "persistentApp", persistentApp );
    pEvent->serialise( nucleusFd );
    delete pEvent;
  } // if
} // dumpHttp

/**
 * sends command to main loop from receiving a signal
 * use a static signal event to prevent signal safe functions in the construction
 * of a new event
 * @param command - the command to send
 * **/
void worker::sendSignalCommand( baseEvent::eCommandType command )
{
  int ret = write( signalFd0, (const void*)&command, sizeof(command) );
  if( ret != sizeof(command) ) fprintf( stderr, "worker::sendSignalCommand failed to write command %d to fd %d\n", command, signalFd0 );
} // sendSignalCommand

/**
 * signal handler
 * keep in mind that libcurl uses SIGALRM for its timeouts
 * **/
void worker::sigHandler( int signo )
{
  switch( signo )
  {
    case SIGTERM:
//      bRunning = false; - use a shutdown command if the intent is to get the worker to exit
      if( (pScriptExec != NULL) && (pScriptExec->getChildPid()>0) )
        kill( pScriptExec->getChildPid(), SIGTERM );
      else
        kill( pid, SIGKILL );   // horrible solution for the curl stuff - rather use typedef cURLpp::OptionTrait< long, cURL::CURLOPT_TIMEOUT > Timeout; or the likes (NoSignal option)
      break;
    case SIGCHLD:
      sendSignalCommand( baseEvent::CMD_CHILD_SIGNAL );
      break;
    default:
      ;
  } // switch
} // sigHandler

