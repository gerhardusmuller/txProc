/** @class appBase
 appBase is the base class for a tServ persistent application
 
 $Id: appBase.cpp 2881 2013-06-09 16:28:13Z gerhardus $
 Versioning: a.b.c a is a major release, b represents changes or new features, c represents bug fixes. 
 @version 1.0.0   17/11/2012    Gerhardus Muller     Script created
 @version 1.1.0		23/06/2012		Gerhardus Muller		 Added bDropPermPriviledges config value
 @version 1.1.1		28/06/2012		Gerhardus Muller		 fixed the pollTimeout for the main loop to prevent it from running the whole time 
 @version 1.2.0		21/08/2012		Gerhardus Muller		 support for a startup info command
 @version 1.3.0		03/10/2012		Gerhardus Muller		 added a startupInfoAvailable and loglevelChanged virtual function callback
 @version 1.4.0		10/10/2012		Gerhardus Muller		 delete txProcSocketLocalPath on exit
 @version 1.5.0		23/10/2012		Gerhardus Muller		 adjusted debug levels for regular events such as a timer running
 @version 1.6.0		08/06/2013		Gerhardus Muller		 main loop by default to block infinitely on file descriptors

 @note

 @todo
 
 @bug

 Copyright Gerhardus Muller
 */

#define __STDC_LIMIT_MACROS 1
#include <stdint.h>
#include <fstream>
#include <iostream>
#include <sstream>
#include <signal.h>
#include <pwd.h>
#include <errno.h>
#include <sys/prctl.h>
#include <sys/types.h> 
#include <sys/stat.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <sys/time.h>

#include "application/appBase.h"
#include "application/optionsBase.h"
#include "application/recoveryLog.h"
#include "utils/utils.h"

// statics and application globals
bool appBase::bRunning = true;
bool appBase::bReopenLog = false;
bool appBase::bResetStats = false;
bool appBase::bDump = false;
bool appBase::bTimerTick = false;
int  appBase::signalFd0 = -1;

optionsBase* pOptions = NULL;
bool bTerminated = false;

/**
 * constructor
 * **/
appBase::appBase( int theArgc, char* theArgv[], const char* theAppName )
  : object( theAppName ),
  appName( theAppName )
{
  argc = theArgc;
  argv = theArgv;
  pRecSock = NULL;
  pSignalSock = NULL;
  pResultEvent = NULL;
  workerPid = -1;
}	// appBase

/**
 * destructor
 * **/
appBase::~appBase()
{
  log.info( log.LOGALWAYS, "~%s", appName.c_str() );
  if( pRecSock != NULL ) delete pRecSock;
  if( pSignalSock != NULL ) delete pSignalSock;
  if( pResultEvent != NULL ) delete pResultEvent;
}	// ~appBase

/**
 * init - creates socket and required functions
 * should be called first by the deriving init
 * @param controllerFd - unix domain socket to submit events to the appBase - the appBase reads 
 * the [1] side and any events to the appBase should be submitted on the [0] side
 * @param dispatcherFd - file descriptor to talk to the dispatcher
 * **/
void appBase::init( )
{
  // configure logging
  log.openLogfile( pOptions->logFile.c_str(), pOptions->bFlushLogs, pOptions->runAsUser.c_str(), pOptions->logGroup.c_str() );
  log.setDefaultLevel( (loggerDefs::eLogLevel) pOptions->defaultLogLevel );
  if( pOptions->bLogStderr )
    log.setLogStdErr( pOptions->bLogStderr );
  else if( pOptions->bLogConsole )
    log.setLogConsole( pOptions->bLogConsole );
  else
    log.setLogConsole( false );
  pOptions->logOptions(); 

  // register signal handling
  // given that it is not possible do static virtual functions - if the derived class needs to support
  // additional signals implement either a replacement signal handler or and a new one and register
  // this for the addional signals
  signal( SIGINT, SIG_IGN );    // will always receive a sigterm from parent
  signal( SIGTERM, appBase::sigHandler );
  signal( SIGALRM, appBase::sigHandler );
  signal( SIGABRT, appBase::sigHandler );

  // reset the signal blocking mask that we inherited
  sigset_t blockmask;
  sigset_t oldmask;
  sigemptyset( &blockmask );    // unblock all the signals
  if( sigprocmask( SIG_SETMASK, &blockmask, &oldmask ) < 0 )
    throw Exception( log, log.ERROR, "init: sigprocmask returned -1" );

  // we communicate via stdin/out to receive/send baseEvent objects
  eventSourceFd = STDIN_FILENO;
  eventReplyFd = STDOUT_FILENO;

  // create the signal pipe - to be able to receive signal events
  unixSocket::createSocketPair( signalFd, "signalFd" );
  signalFd0 = signalFd[0];
  pSignalSock = new unixSocket( signalFd[1], unixSocket::ET_SIGNAL, false, "signalFd1" );
  pSignalSock->setNonblocking( );

  // create the receive socket object - from where our instructions originate
  pRecSock = new unixSocket( eventSourceFd, unixSocket::ET_QUEUE_EVENT, false, "stdin" );
  pRecSock->setPipe();        // this is stdin/stdio
  pRecSock->setNonblocking();
  pRecSock->setThrowEof();
  if( pOptions->defaultLogLevel < 10 ) pRecSock->setLogLevel( 4 ); // otherwise it keeps writing log messages at 20 mS intervals

  // setup the poll structure
  pRecSock->initPoll( 4 );
//  pRecSock->setPollTimeout( 0 );        // dont wait for file descriptors - causes 100% CPU usage
//  pRecSock->setPollTimeout( 500 );        // dont wait for file descriptors - no idea why we would not by default want to block on file descriptors
  pRecSock->addReadFd( eventSourceFd );
  pRecSock->addReadFd( signalFd[1] );

  // open the unix domain socket for requests to be submitted to txProc
  if( ( unlink( pOptions->txProcSocketLocalPath.c_str() ) < 0 ) && ( errno != ENOENT ) )
    log.error( "unlink of %s failed: %s", pOptions->txProcSocketLocalPath.c_str(), strerror(errno) );
  txProcFd = ::socket( AF_UNIX, SOCK_DGRAM, 0 );
  if( txProcFd < 0 ) throw Exception( log, log.ERROR, "init: failed to create txProc socket" );
  struct sockaddr_un ourAddr;
  struct sockaddr_un txProcAddr;
  memset( &ourAddr, 0, sizeof(ourAddr) );
  memset( &txProcAddr, 0, sizeof(txProcAddr) );
  ourAddr.sun_family = AF_LOCAL;
  txProcAddr.sun_family = AF_LOCAL;
  strcpy( ourAddr.sun_path, pOptions->txProcSocketLocalPath.c_str() );
  strcpy( txProcAddr.sun_path, pOptions->txProcSocketPath.c_str() );
  if( bind(txProcFd, (struct sockaddr *)&ourAddr, sizeof(ourAddr)) < 0 ) throw Exception( log, log.ERROR, "init: failed to bind txProc socket path:'%s' - %s", pOptions->txProcSocketLocalPath.c_str(), strerror(errno) );
  if( connect(txProcFd, (struct sockaddr *)&txProcAddr, sizeof(txProcAddr)) < 0 ) throw Exception( log, log.ERROR, "init: failed to connect to txProc socket path:'%s' - %s", pOptions->txProcSocketPath.c_str(), strerror(errno) );

  log.info( log.LOGALWAYS, "init: eventSourceFd:%d eventReplyFd:%d signalFd:%d,%d txProcFd:%d ruid:%d, euid:%d, pid:%d",eventSourceFd,eventReplyFd,signalFd[0],signalFd[1],txProcFd,getuid(),geteuid(),getpid() );

  // we could also have called dropPriviledgePerm() directly here - this has the doubtable advantage
  // of allowing us to drop to a different user than the under priviledged one that txProc uses
  if( pOptions->bDropPermPriviledges )
  {
    bool bResult = dropPriviledge( pOptions->runAsUser.c_str() );
    if( !bResult ) throw Exception( log, log.ERROR, "init: failed to drop priviledges to user %s", pOptions->runAsUser.c_str() );
  } // if

  // retrieve our hostname
  char hostname[256];
  hostname[0] = '\0';
  gethostname( hostname, 255 );
  hostId = hostname;

  // generate a startup event that can be used to monitor startups
  if( !pOptions->applicationEventQueue.empty() )
  {
    baseEvent* pEvent = new baseEvent( baseEvent::EV_PERL );
    pEvent->setDestQueue( pOptions->applicationEventQueue );
    pEvent->addParam( "type", "APPLICATION_STARTUP" );
    pEvent->addParam( "logfile", pOptions->logFile );
    pEvent->addParamAsStr( "pid", getpid() );
    if( !pOptions->applicationEventScript.empty() ) pEvent->setScriptName( pOptions->applicationEventScript );
    pEvent->addParam( "application", pOptions->appBaseName );
    pEvent->addParam( "comment", "this event indicates that the application started" );
    pEvent->serialise( txProcFd );
    delete pEvent;
  } // if

  // other
  termTime = 0;
} // init

/**
 * main processing loop
 * in short listen for events on stdin and process them
 * in the unlikely event this is overridden it is unlikely that this version will be called at all
 * **/
void appBase::main( )
{
  now = time( NULL );

  // set the maintenance timer if required
  if( pOptions->maintInterval > 0 ) alarm( pOptions->maintInterval );

  bRunning = true;
  while( bRunning )
  {
    log.debug( log.LOGSELDOM, "main: bRunning %d termTime %d", appBase::bRunning, termTime );
    try
    {
      // hook for a derived application class
      startLoopProcess();

      bool bReady;
      try
      {
        bReady = pRecSock->multiFdWaitForEvent();
      }
      catch( Exception e )
      {
        log.error( "main: exception from waitForEvent" );
        bRunning = false;   // fatal error
        throw;
      } // catch

      now = time( NULL );
      setNow( now );
      if( bReady )
      {
        int fd = 0;
        // retrieve the available file descriptors and process the events
        while( ( fd = pRecSock->getNextFd() ) >= 0 )
        {
          log.debug( log.LOGSELDOM, "main: fd %d has data", fd );

          // service an event from the outside or a signal generated request
          // read all the available events on a file descriptor before servicing the next one
          try
          {
            if( fd == eventSourceFd )  // fd corresponding to pRecSock - normally stdin
            {
              baseEvent* pEvent = NULL;
              pEvent = baseEvent::unSerialise( pRecSock );

              while( pEvent != NULL )
              {
                try
                {
                  // create a default result event - will be dispatched by sendDone()
                  constructDefaultResultEvent( pEvent );
                  handleNewEvent( pEvent );
                  delete pEvent;
                  pEvent = NULL;
                  pEvent = baseEvent::unSerialise( pRecSock );
                } // try
                catch( Exception e )
                {
                  sendDone();
                  throw;
                } // catch

                sendDone(); // dispatch and delete the result event
              } // while
            } // if( fd == eventSouceFd )
            else if( fd == signalFd[1] )  // service a signal event - received as 4 bytes
            {
              baseEvent::eCommandType theCommand = baseEvent::CMD_NONE;
              int bytesReceived = pSignalSock->read( (char*)(&theCommand), sizeof(theCommand) );
              while( bytesReceived == sizeof(theCommand) )
              {
                handleSignalEvent( theCommand );
                log.debug( log.LOGSELDOM, "main: done with event processing 2" );
                bytesReceived = pSignalSock->read( (char*)(&theCommand), sizeof(theCommand) );
              } // while
            } // if( fd == signalFd[1]
            else
            {
              handleOtherFdEvent( fd );
            } // else
          } // try
          catch( Exception e )
          {
            std::string mess = e.getString();
            if( mess.compare( 0, 15, "read: eof on fd" ) == 0 )
            {
              log.warn( log.LOGALWAYS, "main: %s - exiting immediatly", mess.c_str() );
              bRunning = false;
            } // if
            else
              log.warn( log.LOGALWAYS, "main: ignoring exception in main loop" );
          } // catch
          catch( std::runtime_error e )
          { // json-cpp throws runtime_error
            log.error( "main: ignoring std::runtime_error:'%s'", e.what() );
          } // catch
        } // while fd = pRecSock->getNextFd

        // check if any of the sockets were closed and had errors
        if( (fd == -1) && (pRecSock->getLastErrorFd() > -1) )
        {
          log.error( "main: event fd:%d produced an error - exiting", pRecSock->getLastErrorFd() );
          termTime = now;
        } // if
      } // if( bReady
    } // try
    catch( Exception e )
    {
      log.error( "main: caught exception %s", e.getMessage() );
    } // catch

    // check for shutdown
    if( termTime > 0 )
    {
      if( bRunning )
      {
        // make sure we have a timer running otherwise we can be stuck waiting for an event
        alarm(1);

        if( canExit() )
        {
          // the application is happy for us to exit
          bRunning = false;
        } // if
        else if( (now-termTime) >= (unsigned)pOptions->maxShutdownWaitTime )
        {
          log.info( log.LOGALWAYS, "main: tired waiting for application - exiting" );
          bRunning = false;
        } // if
      } // if bRunning
    } // if terTime > 0
  } // while bRunning

  unlink( pOptions->txProcSocketLocalPath.c_str() );
  log.info( log.LOGALWAYS, "main: exiting bRunning %d terminated %d", appBase::bRunning, bTerminated );
} // main

/**
 * constructs a default resultEvent
 * **/
void appBase::constructDefaultResultEvent( baseEvent* pEvent )
{
  if( pResultEvent != NULL ) delete pResultEvent;
  pResultEvent = new baseEvent( baseEvent::EV_RESULT );
  pResultEvent->setSuccess( true );
  pResultEvent->setRef( pEvent->getRef() );
  pResultEvent->addParam( "generatedby", appName );
  std::string resultQueue;
  if( pEvent->getParam("resultQueue", resultQueue) ) pResultEvent->setDestQueue( resultQueue );
} // constructResultEvent

/**
 * handle a new event
 * can be overridden - optional to first invoke default handling by this function
 * can modify pResultEvent - it will be dispatched after this call by sendDone()
 * **/
void appBase::handleNewEvent( baseEvent* pEvent )
{
  log.info( log.LOGNORMAL ) << "handleNewEvent: " << pEvent->toString();
  if( pEvent->getType() == baseEvent::EV_COMMAND )
    handleEvCommand( pEvent );
  else if( pEvent->getType() == baseEvent::EV_PERL )
    handleEvPerl( pEvent );
  else if( pEvent->getType() == baseEvent::EV_URL )
    handleEvUrl( pEvent );
  else if( pEvent->getType() == baseEvent::EV_RESULT )
    handleEvResult( pEvent );
  else if( pEvent->getType() == baseEvent::EV_BASE )
    handleEvBase( pEvent );
  else if( pEvent->getType() == baseEvent::EV_ERROR )
    handleEvError( pEvent );
  else
    handleEvOther( pEvent );

  log.debug( log.LOGONOCCASION, "handleNewEvent: done with event processing" );
} // handleNewEvent

/**
 * default handling of EV_COMMAND types
 * can be overridden
 * **/
void appBase::handleEvCommand( baseEvent* pEvent )
{
  if( pEvent->getCommand() == baseEvent::CMD_REOPEN_LOG ) 
  {
    log.info( log.LOGMOSTLY, "handleEvCommand: received a CMD_REOPEN_LOG" );
    log.reopenLogfile();
  } // if CMD_REOPEN_LOG
  else if( pEvent->getCommand() == baseEvent::CMD_STATS ) 
  {
    log.info( log.LOGMOSTLY, "main: received a CMD_STATS" );
    std::string time = utils::timeToString( now );
    dumpStats( time );
  } // if CMD_STATS
  else if( pEvent->getCommand() == baseEvent::CMD_RESET_STATS ) 
  {
    log.info( log.LOGMOSTLY, "main: received a CMD_RESET_STATS" );
    resetStats( );
  } // if( CMD_RESET_STATS
  else if( (pEvent->getCommand()==baseEvent::CMD_SHUTDOWN) || (pEvent->getCommand()==baseEvent::CMD_EXIT_WHEN_DONE) || (pEvent->getCommand()==baseEvent::CMD_END_OF_QUEUE) )
  {
    log.info( log.LOGALWAYS, "main: received a %s", pEvent->commandToString() );
    termTime = now;
    prepareToExit( pEvent );
  } // if( CMD_SHUTDOWN
  else if( pEvent->getCommand() == baseEvent::CMD_PERSISTENT_APP ) 
  {
    handlePersistentCommand( pEvent );
  } // if( CMD_PERSISTENT_APP
  else
  {
    handleUnhandledCmdEvents( pEvent );
  } // if( CMD_PERSISTENT_APP
} // handleEvCommand

/**
 * default handling of EV_PERL types
 * can be overridden
 * **/
void appBase::handleEvPerl( baseEvent* pEvent )
{
  log.warn( log.LOGMOSTLY ) << "handleEvPerl: unable to handle:'" << pEvent->toString() << "'";
  markAsRejected( "failed", "no handler" );
} // handleEvPerl

/**
 * default handling of EV_URL types
 * can be overridden
 * **/
void appBase::handleEvUrl( baseEvent* pEvent )
{
  log.warn( log.LOGMOSTLY ) << "handleEvUrl: unable to handle:'" << pEvent->toString() << "'";
  markAsRejected( "failed", "no handler" );
} // handleEvUrl

/**
 * default handling of EV_RESULT types
 * can be overridden
 * **/
void appBase::handleEvResult( baseEvent* pEvent )
{
  log.warn( log.LOGMOSTLY ) << "handleEvResult: unable to handle:'" << pEvent->toString() << "'";
  markAsRejected( "failed", "no handler" );
} // handleEvResult

/**
 * default handling of EV_BASE types
 * can be overridden
 * **/
void appBase::handleEvBase( baseEvent* pEvent )
{
  log.warn( log.LOGMOSTLY ) << "handleEvBase: unable to handle:'" << pEvent->toString() << "'";
} // handleEvBase

/**
 * default handling of EV_ERROR types
 * can be overridden
 * **/
void appBase::handleEvError( baseEvent* pEvent )
{
  log.warn( log.LOGMOSTLY ) << "handleEvError: unable to handle:'" << pEvent->toString() << "'";
} // handleEvError

/**
 * default handling of EV_ other types
 * can be overridden
 * **/
void appBase::handleEvOther( baseEvent* pEvent )
{
  log.warn( log.LOGMOSTLY ) << "handleEvOther: unable to handle:'" << pEvent->toString() << "'";
} // handleEvOther

/**
 * generates a default failure return event
 * @param result - to add to the result event
 * **/
void appBase::markAsRejected( const char* result, const char* reason )
{
  pResultEvent->setSuccess( false );
  if( result!=NULL ) pResultEvent->setResult( std::string(result) );
  if( reason!=NULL ) pResultEvent->addParam( "error", reason );
} // markAsRejected

/**
 * dump state to log file
 * perform optional maintenance tasks
 * typically overridden
 * **/
void appBase::dumpStats( const std::string& time )
{
} // dumpStats

/**
 * resets stats counters
 * typically overridden
 * **/
void appBase::resetStats()
{
} // resetStats

/**
 * handles persistent app command events - typically controlling/configuring the app
 * implement for customised behaviour - default behaviour can exit (typically for code updates),
 * shutdown/unshutdown (application dependent - concept of orderly shutdown but not exiting)
 * change loglevel 
 * can be overridden
 * **/
void appBase::handlePersistentCommand( baseEvent* pCommand )
{
  try
  {
    log.info( log.LOGMOSTLY ) << "handlePersistentCommand: received command:'" << pCommand->toString() << "'";
    std::string cmd = pCommand->getParam( "cmd" );
    if( (cmd.length()==0) ) handleUserPersistentCommand( pCommand );

    if( cmd.compare( "exit" ) == 0 )
    {
      // effectively the same as CMD_SHUTDOWN but typically generated by the application app tool rather than txProc
      termTime = now;
      prepareToExit( pCommand );
    } // if( cmd.compare
    if( cmd.compare( "shutdown" ) == 0 )
    {
      // orderly shutdown of app without exiting
      appShutdown( pCommand );
    } // if( cmd.compare
    else if( cmd.compare( "unshutdown" ) == 0 )
    {
      // can be used after a shutdown to allow the application to continue
      appUnShutdown( pCommand );
    } // if( cmd.compare
    else if( cmd.compare( "loglevel" ) == 0 )
    {
      std::string val = pCommand->getParam( "level" );
      if( val.length()==0 ) throw Exception( log, log.WARN, "handlePersistentCommand: requires parameter level for command '%s'", cmd.c_str() );

      int newLevel = atoi( val.c_str() );
      log.info( log.LOGALWAYS, "handlePersistentCommand: setting loglevel to %d", newLevel );
      pOptions->defaultLogLevel = newLevel;
      log.setDefaultLevel( (loggerDefs::eLogLevel) pOptions->defaultLogLevel );
      loglevelChanged( newLevel );
    } // if( cmd.compare
    else if( cmd.compare( "startupinfo" ) == 0 )
    {
      ownQueue = pCommand->getParam( "ownqueue" );
      workerPid = pCommand->getParamAsInt( "workerpid" );
      startupInfoAvailable();
      log.info( log.LOGALWAYS, "handlePersistentCommand: ownQueue:%s workerPid:%d", ownQueue.c_str(), workerPid );
    } // if( cmd.compare
    else
      handleUserPersistentCommand( pCommand );
  } // try
  catch( Exception e )
  {
    pResultEvent->addParam( "error", e.getMessage() );
  } // catch
} // handlePersistentCommand

/**
 * handles user extensions to persistent commands
 * should be implemented to do something useful
 * the extensions are either additional 'cmd' values or events not using the 'cmd' syntax
 * can be overridden
 * **/
void appBase::handleUserPersistentCommand( baseEvent* pCommand )
{
  log.warn( log.LOGMOSTLY ) << "handleUserPersistentCommand: unable to handle:'" << pCommand->toString() << "'";
} // handleUserPersistentCommand

/**
 * any cmd events of a type not handled elsewhere
 * can be overridden
 * **/
void appBase::handleUnhandledCmdEvents( baseEvent* pCommand )
{
  log.warn( log.LOGMOSTLY ) << "handleUnhandledCmdEvents: unable to handle:'" << pCommand->toString() << "'";
} // handleUnhandledCmdEvents

/**
 * sends a done message back to the parent
 * **/
void appBase::sendDone()
{
  if( pResultEvent == NULL )
  {
    pResultEvent = new baseEvent( baseEvent::EV_RESULT );
    pResultEvent->setSuccess( true );
  }
  pResultEvent->serialise( eventReplyFd, baseEvent::FD_PIPE );
  log.debug( log.LOGNORMAL, "sendDone:'%s' fd:%d", pResultEvent->toString().c_str(), eventReplyFd );
  delete pResultEvent;
  pResultEvent = NULL;
} // sendDone

/**
 * handles received signal events (via the signalFd socket)
 * **/
void appBase::handleSignalEvent( baseEvent::eCommandType theCommand )
{
  if( theCommand == baseEvent::CMD_TIMER_SIGNAL )
  { 
    // maintenance tasks
    log.info( log.LOGSELDOM, "handleSignalEvent: received a CMD_TIMER_SIGNAL" );
    execMaintenance();

    // restart the maintenance timer
    alarm( pOptions->maintInterval );
  } // if CMD_TIMER_SIGNAL
} // handleSignalEvent

/**
 * handles events on other file descriptors
 * **/
void appBase::handleOtherFdEvent( int fd )
{
  log.error( "handleOtherFdEvent: unhandled data on fd:%d", fd );
  close( fd );
  // TODO should really switch to the epoll class to be able to remove the file descriptor
} // handleOtherFdEvent

/**
 * drops priviledges from root to the user and the users default group
 * @param user
 * @return true on success
 * **/
bool appBase::dropPriviledge( const char* user )
{
  struct passwd* pw = getpwnam( user );
  if( pw == NULL ) 
  {
    log.error( "dropPriviledge: could not find user %s", user );
    return false;
  }
  int userId = pw->pw_uid;
  int groupId = pw->pw_gid;
  log.info( log.LOGALWAYS, "dropPriviledge: to user:'%s' uid:%d gid:%d pid:%d", user, userId, groupId, getpid() );
  
  int result = setgid( groupId );
  if( result == -1 )
  {
    log.error( "dropPriviledge: failed on setgid - %s", strerror(errno) );
    return false;
  }
  result = utils::dropPriviledgePerm( userId );
  if( result != 0 )
  {
    log.error( "dropPriviledge: failed on setuid - %s", strerror(errno) );
    return false;
  }
  log.info( log.LOGALWAYS, "dropPriviledge: priviledges now are ruid:%d, euid:%d", getuid(), geteuid() );

  // set the process to be dumpable
  if( prctl( PR_SET_DUMPABLE, 1, 0, 0, 0 ) == -1 )
    log.error( "dropPriviledge: prctl( PR_SET_DUMPABLE ) error - %s", strerror( errno ) );

  return true;
} // dropPriviledge

/**
 * sends command to main loop from receiving a signal
 * use a static signal event to prevent signal safe functions in the construction
 * of a new event
 * @param command - the command to send
 * **/
void appBase::sendSignalCommand( baseEvent::eCommandType command )
{
  int ret = write( signalFd0, (const void*)&command, sizeof(command) );
  if( ret != sizeof(command) ) fprintf( stderr, "appBase::sendSignalCommand failed to write command %d to fd %d\n", command, signalFd0 );
} // sendSignalCommand

/**
 * signal handler
 * **/
void appBase::sigHandler( int signo )
{
  switch( signo )
  {
    case SIGTERM:
      bRunning = false;
      bTerminated = true;
      break;
    case SIGALRM:
      sendSignalCommand( baseEvent::CMD_TIMER_SIGNAL );
      break;
    case SIGABRT:
      fprintf( stderr, "appBase::sigHandler received a SIGABRT\n" );
      break;
    default:
      fprintf( stderr, "appBase::sigHandler cannot handle signal %d\n", signo );
  } // switch
} // sigHandler

void recoveryLog::writeEntry(baseEvent* a, char const* b, char const* c, char const* d) {;}
