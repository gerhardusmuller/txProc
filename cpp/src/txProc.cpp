/** @class txProc
 txProc - this is the main application of the transaction processing engine or server.
 It provides fine grained load scheduling, management and queueing services as well as
 built in support te rerun tasks that have failed.

 The following signals are supported
 SIGUSR1 - to reset stats
 SIGUSR2 - reopen logs
 SIGHUP - to dump state and statistics
 SIGALRM - alarm timer - to dump the stats and run maintenance tasks

 to enable core dumps: 
 sudo echo "1" > /proc/sys/kernel/core_uses_pid
 ulimit -c unlimited

 txProc can be extensively reconfigured on the fly by sending the appropriate instructions
 vi txProcAdmin.pl
 
 $Id: txProc.cpp 2943 2013-11-19 09:30:01Z gerhardus $
 Versioning: a.b.c a is a major release, b represents changes or new features, c represents bug fixes. 
 @version 1.0.0   02/12/2010    Gerhardus Muller    script created
 @version 1.1.0   19/04/2010    Gerhardus Muller    dropping priviledges only temporarily
 @version 1.2.0 	30/03/2011    Gerhardus Muller    added labels for createSocketPair
 @version 1.3.0 	05/06/2013    Gerhardus Muller    createSocketPair prototype changed - not closing upper level sockets
 @version 1.4.0		20/06/2013		Gerhardus Muller		theNetworkIf for the fdsToRemainOpen list
 @version 1.5.0		06/11/2013		Gerhardus Muller		compilation under debian

 @note

 @todo
 
 @bug

 Copyright Notice
 */

#include <fstream>
#include <iostream>
#include <sstream>
#include <signal.h>
#include <errno.h>
#include <string.h>
#include <sys/types.h> 
#include <sys/wait.h>
#include <pwd.h>
#include <sys/stat.h>
#include <fcntl.h>

#include "../src/txProc.h"
#include "networkIf/networkIf.h"
#include "nucleus/nucleus.h"
#include "nucleus/recoveryLog.h"
#include "utils/utils.h"
#include <sys/resource.h>

bool txProc::bRunning = true;
bool txProc::bChildSignal = false;
int  txProc::signalFd0 = 0;
options* pOptions = NULL;
txProc* theServer = NULL;

#define BFORK      // used for valgrind testing

/**
 Construction
 */
txProc::txProc( int theArgc, char* theArgv[] )
  : object( "txProc" )
{
  memset( mainFd, 0, sizeof( mainFd ) );
  memset( nucleusFd, 0, sizeof( nucleusFd ) );
  memset( networkIfFd, 0, sizeof( networkIfFd ) );
  memset( signalFd, 0, sizeof( signalFd ) );
  pRecSock = NULL;
  pSignalSock = NULL;
  pNucleus = NULL;
  pNetworkIf = NULL;
  pRecoveryLog = NULL;
  nucleusPid = 0;
  networkIfPid = 0;
  pid = getpid( );
  argc = theArgc;
  argv = theArgv;
}	// txProc

/**
 * deletes objects
 * **/
void txProc::cleanupObj( )
{
  if( pNucleus != NULL ) {delete pNucleus; pNucleus=NULL;};
  if( pNetworkIf != NULL )     {delete pNetworkIf; pNetworkIf=NULL;};
  if( pRecSock != NULL )    {delete pRecSock; pRecSock=NULL;};
  if( pSignalSock != NULL ) {delete pSignalSock; pSignalSock=NULL;};
  if( pRecoveryLog != NULL ){delete pRecoveryLog; pRecoveryLog=NULL;};
} // cleanupObj

/**
 Destruction
 */
txProc::~txProc()
{
  log.info( log.LOGNORMAL, "txProc::~txProc" );
  log.info( log.LOGNORMAL, "txProc::~txProc pNucleus %p pNetworkIf %p pRecSock %p, pSignalSock %p, pRecoveryLog %p", pNucleus, pNetworkIf, pRecSock, pSignalSock, pRecoveryLog );
  waitForChildrenToExit( );
  cleanupObj();

  if( mainFd[0] != 0 ) close( mainFd[0] );
  if( mainFd[1] != 0 ) close( mainFd[1] );
  if( nucleusFd[0] != 0 ) close( nucleusFd[0] );
  if( nucleusFd[1] != 0 ) close( nucleusFd[1] );
  if( networkIfFd[0] != 0 ) close( networkIfFd[0] );
  if( networkIfFd[1] != 0 ) close( networkIfFd[1] );
  if( signalFd[0] != 0 ) close( signalFd[0] );
  if( signalFd[1] != 0 ) close( signalFd[1] );

  log.info( log.LOGNORMAL, "txProc::~txProc pNucleus %p pNetworkIf %p pRecSock %p, pSignalSock %p, pRecoveryLog %p", pNucleus, pNetworkIf, pRecSock, pSignalSock, pRecoveryLog );
}	// ~txProc

/**
 Standard logging call - produces a generic text version of the txProc.
 Memory allocation / deleting is handled by this txProc.
 @return pointer to a string describing the state of the txProc.  The string has an unspecified lifetime and is not multi-thread safe
 */
std::string txProc::toString( )
{
  std::ostringstream oss;
  oss << typeid(*this).name() << " pid " << getpid( ) << " nucleus pid " << nucleusPid;
	return oss.str();
}	// toString

/**
 * init - creates networkIf and required functions
 * **/
void txProc::init( )
{
  log.info( log.LOGALWAYS ) << "init: txProc - build no " << buildno << " build time " << buildtime;
  unixSocket::createSocketPair( mainFd, "mainFd", false );
  unixSocket::createSocketPair( nucleusFd, "nucleusFd", false );
  unixSocket::createSocketPair( networkIfFd, "networkIfFd", false );
  unixSocket::createSocketPair( signalFd, "signalFd" );
  log.info( log.LOGALWAYS, "init: txProc %d %d nucleusFd %d %d networkIfFd %d %d signalFd %d %d", mainFd[0], mainFd[1], nucleusFd[0], nucleusFd[1], networkIfFd[0], networkIfFd[1], signalFd[0], signalFd[1] );
  pRecoveryLog = new recoveryLog( pOptions->logBaseDir.c_str(), false, pOptions->logrotatePath, pOptions->runAsUser, pOptions->logGroup, pOptions->logFilesToKeep ); // do not rotate here as well
  baseEvent::theRecoveryLog = pRecoveryLog;
  
  // create a networkIf object
  eventSourceFd = mainFd[1];
  signalFd0 = signalFd[0];
  pSignalSock = new unixSocket( signalFd[1], unixSocket::ET_SIGNAL, false, "signalFd1" );
  pSignalSock->setNonblocking( );
  pRecSock = new unixSocket( mainFd[1], unixSocket::ET_QUEUE_EVENT, false, "mainFd1" );
  pRecSock->setNonblocking( );
  pRecSock->initPoll( 2 );
  pRecSock->addReadFd( mainFd[1] );
  pRecSock->addReadFd( signalFd[1] );
  
  // retrieve our hostname
  char hostname[256];
  hostname[0] = '\0';
  gethostname( hostname, 255 );
  hostId = hostname;

  bAutoFork = true;
} // init

/**
 * recovers the transactions in a recovery file
 * **/
void txProc::recover( )
{
  try
  {
    init( );
    forkNucleus( true );
    
    bool bResult = dropPriviledge( pOptions->runAsUser.c_str() );
    if( !bResult )
      throw Exception( log, log.ERROR, "recover: failed to drop priviledges to user %s", pOptions->runAsUser.c_str() );

    sleep( 1 ); // give time for the nucleus to start
    pRecoveryLog->initRecovery( pOptions->recoverFile.c_str(), nucleusFd[0] );

    while( bRunning && pRecoveryLog->recover() )
    {
      sleep( 1 );
      if( bChildSignal )
      {
        bChildSignal = false;
        handleChildDone( );
      } // if
    } // while
  } // try
  catch( Exception e )
  {
    // exception has been logged - exit
    bRunning = false;
    termChildren( );  // send a sigterm to children
  } // catch
  
  // tell nucleus to exit when done - wait for the nucleus
  sendCommandToChildren( baseEvent::CMD_EXIT_WHEN_DONE );
  log.info( log.LOGALWAYS, "recover: done - exiting" );
  bRunning = false;
  bAutoFork = false;
  
  while( nucleusPid != 0 )
  {
    if( !handleChildDone( ) )
      sleep( 1 );
  } // if
} // recover

/**
 * run as a daemon
 * **/
void txProc::runAsDaemon( )
{
  int fd0, fd1, fd2;
  struct rlimit rl;
  
  getrlimit( RLIMIT_NOFILE, &rl );

  // become a session leader to loose the controlling TTY
  if( ( pid = fork() ) < 0 )
  {
    fprintf( stderr, "runAsDaemon: failed to fork\n" );
    exit( 1 );
  }
  else if( pid != 0 )
  {
    // parent logs the pid so that logrotate and others can find it, then exits
    log.info( log.LOGALWAYS, "runAsDaemon: PID %d", pid );
    FILE* pidFile = fopen( pOptions->pidName.c_str(), "w" );
    if( pidFile == NULL )
      log.warn( log.LOGALWAYS, "runAsDaemon: failed to write pid to %s", pOptions->pidName.c_str() );
    else
    {
      fprintf( pidFile, "%d", pid );
      fclose( pidFile );
    }
    exit( 0 );
  } // else parent
  setsid( );

  // close the open standard file handle - cannot close all as assumed
  // the diva sdk has already opened its log
  //  if( rl.rlim_max == RLIM_INFINITY )
  //    rl.rlim_max = 1024;
  rl.rlim_max = 3;
  for( unsigned int i = 0; i < rl.rlim_max; i++ )
    close( i );
  
  // attach file descriptors 0,1,2 to /dev/null
  fd0 = open( "/dev/null", O_RDWR );
  fd1 = dup( 0 );
  fd2 = dup( 0 );
  if( (fd0 != 0) || (fd1 != 1) || (fd2 != 2) )
    log.warn( log.LOGALWAYS, "runAsDaemon: funny file descriptors %d, %d, %d", fd0, fd1, fd2 );

  pid = getpid( );
} // runAsDaemon

/**
 * dump as http - url reporting
 * **/
void txProc::dumpHttp( const std::string& time )
{
  if( time.length() == 0 )
  {
    log.error( "dumpHttp: time should have a value" );
    return;
  } // if
//  if( pOptions->statsUrl.length() > 0 )
//  {
//    baseEvent* pEvent = new baseEvent( baseEvent::EV_URL, pOptions->statsQueue.c_str() );
//    pEvent->setUrl( pOptions->statsUrl );
//    pEvent->addParam( "cmd", "stats" );
//    pEvent->addParam( "name", typeid(*this).name() );
//    pEvent->addParam( "time", time );
//    pEvent->addParam( "hostid", hostId );
//    pEvent->serialise( nucleusFd[0] );
//    delete pEvent;
//  } // if
} // dumpHttp

/**
 * main txProc processing loop
 * **/
void txProc::main( )
{
  unsigned int termTime = 0;
  now = time( NULL );

  try
  {
    init( );
#ifdef BFORK
    if( pOptions->bStartNucleus ) forkNucleus( false );
    if( pOptions->bStartSocket ) forkNetworkIf( );
#else
    // used for valgrind testing
#if 1
    pNucleus = new nucleus( nucleusFd, mainFd[0], pRecoveryLog, false, argc, argv, networkIfFd[0] );
    pNucleus->init();
    pNucleus->main();
    log.info( log.LOGALWAYS, "forkNucleus: pNucleus->main returned" );
    delete pNucleus;
    pNucleus = NULL;
#else
    pNetworkIf = new networkIf( networkIfFd, nucleusFd[0], mainFd[0], pRecoveryLog, argc, argv );
    pNetworkIf->main( );
    log.info( log.LOGALWAYS, "main: pNetworkIf->main returned" );
    delete pNetworkIf;
    pNetworkIf = NULL;
#endif
    bRunning = false;
#endif

    // start the alarm if regular stats are required
    if( ( pOptions->statsInterval > 0 ) )
      alarm( pOptions->statsInterval );

    // wait for children to exit
    while( bRunning )
    {
      log.debug( log.LOGNORMAL, "main: bRunning %d", bRunning );
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

      now = time( NULL );
      if( bReady )
      {
        int fd = 0;
        // retrieve the available file descriptors and process the events
        while( ( fd = pRecSock->getNextFd( ) ) > 0 )
        {
          log.debug( log.LOGNORMAL, "main: fd %d has data", fd );

          // service an event from the outside or a signal generated request
          try
          {
            if( fd == eventSourceFd )
            {
              baseEvent* pEvent = NULL;
              pEvent = baseEvent::unSerialise( pRecSock );

              while( pEvent != NULL )
              {
                if( pEvent->getType() == baseEvent::EV_COMMAND )
                {
                  if( pEvent->getCommand( ) == baseEvent::CMD_STATS ) 
                  { // request to dump stats after receiving a SIGHUP
                    log.info( log.LOGMOSTLY, "main: received a CMD_STATS" );
                    std::string time = utils::timeToString( now );
                    baseEvent* pChildCommand = new baseEvent( baseEvent::EV_COMMAND );
                    pChildCommand->setCommand( baseEvent::CMD_STATS );
                    pChildCommand->addParam( "time", time );
                    if( pOptions->statsChildrenAddress.length() > 0 )
                      notifySlaveStatsServers( pChildCommand );
                    sendCommandToChildren( pChildCommand );
                    delete pChildCommand;

                    dumpHttp( time );
                  } // if CMD_STATS
                  else if( pEvent->getCommand( ) == baseEvent::CMD_TIMER_SIGNAL )
                  { // the alarm is used to generate stats regularly
                    alarm( pOptions->statsInterval );
                    log.info( log.LOGNORMAL, "main: received a CMD_TIMER_SIGNAL" );
                    time_t now1 = now;
                    struct tm* t = localtime( &now1 );
                    log.debug( log.LOGONOCCASION, "main: bStatsHttp hour %d start %d stop %d", t->tm_hour, pOptions->statsHourStart, pOptions->statsHourStop );
                    if( (t->tm_hour >= pOptions->statsHourStart) && (t->tm_hour < pOptions->statsHourStop) )
                    {
                      std::string time = utils::timeToString( now );
                      baseEvent* pChildCommand = new baseEvent( baseEvent::EV_COMMAND );
                      pChildCommand->setCommand( baseEvent::CMD_STATS );
                      pChildCommand->addParam( "time", time );
                      if( pOptions->statsChildrenAddress.length() > 0 )
                        notifySlaveStatsServers( pChildCommand );
                      sendCommandToChildren( pChildCommand );
                      delete pChildCommand;

                      dumpHttp( time );
                    } // if
                  } // if CMD_TIMER_SIGNAL
                  else if( pEvent->getCommand( ) == baseEvent::CMD_CHILD_SIGNAL ) 
                  {
                    log.warn( log.LOGALWAYS,"main: received a CMD_CHILD_SIGNAL" );
                    handleChildDone( );
                  } // if CMD_CHILD_SIGNAL
                  else if( pEvent->getCommand( ) == baseEvent::CMD_REOPEN_LOG ) 
                  {
                    log.info( log.LOGMOSTLY, "main: received a CMD_REOPEN_LOG" );
                    sendCommandToChildren( baseEvent::CMD_REOPEN_LOG );
                    log.reopenLogfile( );
                    pRecoveryLog->reOpen( );
                  } // if CMD_REOPEN_LOG
                  else if( pEvent->getCommand( ) == baseEvent::CMD_RESET_STATS ) 
                  {
                    log.info( log.LOGMOSTLY, "main: received a CMD_RESET_STATS" );
                    sendCommandToChildren( baseEvent::CMD_RESET_STATS );
                  } // if( CMD_RESET_STATS
                  else if( (pEvent->getCommand( ) == baseEvent::CMD_SHUTDOWN) || (pEvent->getCommand( ) == baseEvent::CMD_EXIT_WHEN_DONE) )
                  {
                    log.info( log.LOGALWAYS, "main: received a %s", pEvent->commandToString() );
                    // shutdown the controller and networkIf - allow the nucleus to run to service the 
                    // remainder of the controller events
                    // TODO: the correct behaviour would be to only prevent new connections from the 
                    // outside as we may have establised TCP connections waiting for responses
                    if( termTime == 0 )
                    {
                      termTime = now;
                      bAutoFork = false;
                    } // if
                    if( networkIfPid > 0 ) sendCommandToChild( baseEvent::CMD_SHUTDOWN, networkIfFd[0] );
                    if( nucleusPid > 0 ) sendCommandToChild( baseEvent::CMD_EXIT_WHEN_DONE, nucleusFd[0] );
                  } // if( CMD_SHUTDOWN
                  else if( pEvent->getCommand( ) == baseEvent::CMD_MAIN_CONF ) 
                  {
                    reconfigure( pEvent );
                  } // if( CMD_MAIN_CONF
                  else
                    log.warn( log.LOGALWAYS,"main: baseEvent failed to handle command %d", pEvent->getCommand( ) );
                } // if baseEvent::EV_COMMAND
                else
                  log.error( "main: unable to handle event %s", typeid(*pEvent).name() );

                log.debug( log.LOGNORMAL, "main: done with event processing 1" );
                delete pEvent;
                pEvent = NULL;
                pEvent = baseEvent::unSerialise( pRecSock );
              } // while
            } // if( fd == eventSouceFd )
            else if( fd == signalFd[1] )  // service a signal event - repost as normal events
            {
              baseEvent::eCommandType theCommand = baseEvent::CMD_NONE;
              int bytesReceived = pSignalSock->read( (char*)(&theCommand), sizeof(theCommand) );
              while( bytesReceived == sizeof(theCommand) )
              {
                baseEvent* pEvent = new baseEvent( baseEvent::EV_COMMAND );
                pEvent->setCommand( theCommand );
                log.info( log.LOGMOSTLY, "main: redispatching signal command '%s'", pEvent->toString().c_str() );
                pEvent->serialise( mainFd[0] );
                delete pEvent;

                bytesReceived = pSignalSock->read( (char*)(&theCommand), sizeof(theCommand) );
              } // while
            } // if( fd == signalFd[1]
          } // try
          catch( Exception e )
          {
            log.warn( log.LOGALWAYS, "main: ignoring exception in main loop" );
          } // catch
          catch( std::runtime_error e )
          { // json-cpp throws runtime_error
            log.error( "main: ignoring std::runtime_error:'%s'", e.what() );
          } // catch
        } // while fd = pRecSock->getNextFd
      } // if( bReady

      // signal the nucleus to exit when done
      if( termTime > 0 )
      {
        if( nucleusPid > 0 )
        {
          bRunning = false;
          log.info( log.LOGALWAYS, "main: exiting" );
        } // if
      } // if
    } // while bRunning
  } // try
  catch( Exception e )
  {
    // exception has been logged - exit
    log.warn( log.LOGALWAYS, "main: ignoring 1 exception in main" );
    bRunning = false;
  } // catch
  catch( std::runtime_error e )
  { // json-cpp throws runtime_error
    log.warn( log.LOGALWAYS, "main: ignoring std::runtime_error:'%s'", e.what() );
    bRunning = false;
  } // catch
} // main


/**
 * handles a reconfigure command
 * **/
void txProc::reconfigure( baseEvent* pCommand )
{
  try
  {
    log.info( log.LOGMOSTLY ) << "reconfigure: received command '" << pCommand->toString() << "'";
    std::string cmd = pCommand->getParam( "cmd" );
    if( (cmd.length()==0) )
      throw Exception( log, log.WARN, "reconfigure: require parameter cmd" );

    if( cmd.compare( "loglevel" ) == 0 )
    {
      std::string val = pCommand->getParam( "val" );
      if( val.length()==0 )
        throw Exception( log, log.WARN, "reconfigure: require parameter val for command '%s'", cmd.c_str() );

      int newLevel = atoi( val.c_str() );
      log.info( log.LOGALWAYS, "reconfigure: setting loglevel to %d", newLevel );
      pOptions->defaultLogLevel = newLevel;
      log.setDefaultLevel( (loggerDefs::eLogLevel) pOptions->defaultLogLevel );
    } // if( cmd.compare
    else
      log.warn( log.LOGALWAYS, "reconfigure: cmd '$s' not supported", cmd.c_str() );
  } // try
  catch( Exception e )
  {
  } // catch
} // reconfigure


/**
 * sends a UDP stats notification to any slave servers
 * @param pCommand - the stats command
 * **/
void txProc::notifySlaveStatsServers( baseEvent* pCommand )
{
  if( pOptions->statsChildrenAddress.length() == 0 ) return;
  std::string payload = pCommand->serialiseToString( );
  baseEvent* pUdp = new baseEvent( baseEvent::EV_COMMAND );
  pUdp->setCommand( baseEvent::CMD_SEND_UDP_PACKET );
  pUdp->addParam( "service", pOptions->statsChildrenService );
  pUdp->addParam( "payload", payload );
  log.info( log.LOGNORMAL, "notifySlaveStatsServers: to %s, service %s", pOptions->statsChildrenAddress.c_str(), pOptions->statsChildrenService.c_str() );
  if( log.wouldLog( log.LOGONOCCASION ) )log.debug( log.LOGONOCCASION, "notifySlaveStatsServers: payload %s", payload.c_str() );

  std::string::size_type start = 0;
  std::string::size_type end;
  while( (end = pOptions->statsChildrenAddress.find( ',', start ) ) != std::string::npos )
  {
    std::string address( pOptions->statsChildrenAddress, start, end-start );
    pUdp->addParam( "address", address ); // actually a replace
    pUdp->serialise( networkIfFd[0] );
    log.debug( log.LOGNORMAL, "notifySlaveStatsServers: to %s startI %d endI %d", address.c_str(), start, end );
    start = end+1;
  } // while
  if( start < pOptions->statsChildrenAddress.length() )
  {
    std::string address( pOptions->statsChildrenAddress, start, pOptions->statsChildrenAddress.length() );
    pUdp->addParam( "address", address ); // actually a replace
    log.debug( log.LOGNORMAL, "notifySlaveStatsServers: 1 to %s startI %d endI %d", address.c_str(), start, end );
    pUdp->serialise( networkIfFd[0] );
  } // if
  delete pUdp; 
} // notifySlaveStatsServers

/**
 * sends a kill to all children
 * **/
void txProc::termChildren( )
{
  log.info( log.LOGALWAYS, "termChildren" );
  signalChildren( SIGTERM );
} // termChildren

/**
 * sends command to main loop from receiving a signal
 * use a static signal event to prevent signal safe functions in the construction
 * of a new event
 * @param command - the command to send
 * **/
void txProc::sendSignalCommand( baseEvent::eCommandType command )
{
  int ret = write( signalFd0, (const void*)&command, sizeof(command) );
  if( ret != sizeof(command) ) fprintf( stderr, "txProc::sendSignalCommand failed to write command %d to fd %d\n", command, signalFd0 );
} // sendSignalCommand

/**
 * sends command requests to the children
 * @param command - the command to send
 * @param fd - the file descriptor for the child
 * **/
void txProc::sendCommandToChild( baseEvent::eCommandType command, int fd )
{
  if( fd < 1 ) return;
  baseEvent* pEvent = new baseEvent( baseEvent::EV_COMMAND );
  pEvent->setCommand( command );
  pEvent->serialise( fd );
  delete pEvent;
} // sendCommandToChild

/**
 * sends command requests to the children
 * @param command - the command to send
 * **/
void txProc::sendCommandToChildren( baseEvent::eCommandType command )
{
  baseEvent* pEvent = new baseEvent( baseEvent::EV_COMMAND );
  pEvent->setCommand( command );
  sendCommandToChildren( pEvent );
  delete pEvent;
} // sendCommandToChildren

/**
 * sends command requests to the children
 * @param pCommand - the command to send
 * **/
void txProc::sendCommandToChildren( baseEvent* pCommand )
{
  if( nucleusPid != 0 ) 
    pCommand->serialise( nucleusFd[0] );
  if( networkIfPid != 0 ) 
    pCommand->serialise( networkIfFd[0] );
} // sendCommandToChildren

/**
 * signal children
 * @param sig - the signal to send
 * **/
void txProc::signalChildren( int sig )
{
  log.debug( log.LOGNORMAL, "signalChildren sig %s", strsignal( sig ) );
  if( nucleusPid != 0 ) 
    kill( nucleusPid, sig );
  if( networkIfPid != 0 ) 
    kill( networkIfPid, sig );
} // signalChildren

/**
 * handle a child that exited - restart if we should be running
 * **/
bool txProc::handleChildDone( )
{
  int status;
  int childPid = -1;
  bool bChildExited = false;
  
  log.debug( log.LOGNORMAL, "handleChildDone:" );
  bool bDone = false;
  do
  { 
    childPid = waitpid( -1, &status, WNOHANG );
    if( childPid == 0 )         // no children needing attention
    {
      bDone = true;
    } // if
    else if( childPid == -1 )   // potential error condition
    {
      if( errno == EINTR )
      {
        log.debug( log.LOGNORMAL, "handleChildDone: wait was interrupted" );
      } // if
      else if( errno == ECHILD )
      {
        log.info( log.LOGNORMAL, "handleChildDone: no more children" );
        bDone = true;
      } // else if
      else
      {
        throw Exception( log, log.ERROR, "handleChildDone: wait error: %s", strerror( errno ) );
      } // else
    } // else if
    else                        // child pid returned
    {
      log.warn( log.LOGALWAYS, "handleChildDone: child %d exited %s", childPid, utils::printExitStatus( status ).c_str() );
      bChildExited = true;
      
      // auto restart
      if( bAutoFork )
      {
        if( childPid == nucleusPid ) 
        {
          log.warn( log.LOGALWAYS, "handleChildDone: about to restart nucleus" );
          nucleusPid = 0;
          forkNucleus( );
        } // if(
        else if( childPid == networkIfPid ) 
        {
          log.warn( log.LOGALWAYS, "handleChildDone: about to restart networkIf" );
          networkIfPid = 0;
          forkNetworkIf( );
        } // if(
        else
          log.warn( log.LOGALWAYS, "handleChildDone: unknown pid %d", childPid );
      } // if( bRunning
      else
      {
        if( childPid == nucleusPid ) 
        {
          log.warn( log.LOGALWAYS, "handleChildDone: nucleus exited" );
          nucleusPid = 0;
        } // if(
        else if( childPid == networkIfPid ) 
        {
          log.warn( log.LOGALWAYS, "handleChildDone: networkIf exited" );
          networkIfPid = 0;
        } // if(
        else
          log.warn( log.LOGALWAYS, "handleChildDone: unknown pid %d", childPid );
      } // else( bRunning
    } // else
  } while( !bDone );

  return bChildExited;
} // handleChildDone

/**
 * wait for children to exit
 * **/
void txProc::waitForChildrenToExit( )
{
  log.info( log.LOGALWAYS, "waitForChildrenToExit:" );

  // wait for the children to exit - eventually wait returns ECHILD - no children
  int pid = 0;
  int status = 0;
  pid = wait( &status );
  while( ( pid != -1 ) || (( pid==-1)&&(errno==EINTR)) )
  {
    if( pid > 0 )
    {
      if( pid == nucleusPid ) 
        log.info( log.LOGALWAYS, "waitForChildrenToExit: nucleus exited" );
      else if( pid == networkIfPid ) 
        log.info( log.LOGALWAYS, "waitForChildrenToExit: networkIf exited" );
      else
        log.warn( log.LOGALWAYS, "waitForChildrenToExit: unknown pid %d", pid );
    } // if
    pid = wait( &status );
  } // while
} // waitForChildrenToExit

/**
 * fork the nucleus process
 * @param bRecovery - true if it is a recovery instance
 * **/
void txProc::forkNucleus( bool bRecovery )
{
  if( ( nucleusPid = fork( ) ) < 0 )
    log.error( "forkNucleus: failed to fork" );
  else if( nucleusPid == 0 )   // child
  {
    try
    {
      // the new and main() has to be in a try / catch otherwise an uncaught
      // exception kills the other children as well
      pNucleus = new nucleus( nucleusFd, mainFd[0], pRecoveryLog, bRecovery, argc, argv, networkIfFd[0] );
      pNucleus->init();
      pNucleus->main();
      log.info( log.LOGALWAYS, "forkNucleus: pNucleus->main returned" );
      delete pNucleus;
      pNucleus = NULL;
      exit( 0 );
    } // try
    catch( std::runtime_error e )
    { // json-cpp throws runtime_error
      log.error( "forkNucleus: caught std::runtime_error in forkNucleus child process:'%s'", e.what() );
      if( pNucleus != NULL ) delete pNucleus;
      sleep( 1 ); // limit the rate of respawning
      exit( 1 );
    } // catch
    catch( ... )
    {
      log.error( "forkNucleus: caught exception in forkNucleus child process - pNucleus:%p", pNucleus );
      if( pNucleus != NULL ) delete pNucleus;
      sleep( 1 ); // limit the rate of respawning
      exit( 1 );
    } // catch
  } // if child
  else  // parent
  {
    log.info( log.LOGALWAYS, "forkNucleus: nucleus has pid %d fd %d, %d", nucleusPid, nucleusFd[0], nucleusFd[1] );
  } // else parent
} // forkNucleus

/**
 * fork the networkIf process
 * **/
void txProc::forkNetworkIf( )
{
  if( ( networkIfPid = fork( ) ) < 0 )
    log.error( "forkNetworkIf: failed to fork" );
  else if( networkIfPid == 0 )   // child
  {
    try
    {
      // the new and main() has to be in a try / catch otherwise an uncaught
      // exception kills the other children as well
      pNetworkIf = new networkIf( networkIfFd, nucleusFd[0], mainFd[0], pRecoveryLog, argc, argv );
      pNetworkIf->main( );
      log.info( log.LOGALWAYS, "forkNetworkIf: pNetworkIf->main returned" );
      delete pNetworkIf;
      pNetworkIf = NULL;
      exit( 0 );
    } // try
    catch( std::runtime_error e )
    { // json-cpp throws runtime_error
      log.error( "forkNetworkIf: caught std::runtime_error in forkNucleus child process:'%s'", e.what() );
      if( pNetworkIf != NULL ) delete pNetworkIf;
      sleep( 1 ); // limit the rate of respawning
      exit( 1 );
    } // catch
    catch( ... )
    {
      log.error( "forkNetworkIf: caught exception in forkNetworkIf child process" );
      if( pNetworkIf != NULL ) delete pNetworkIf;
      sleep( 1 ); // limit the rate of respawning
      exit( 1 );
    } // catch
  } // if child
  else  // parent
  {
    log.info( log.LOGALWAYS, "forkNetworkIf: networkIf has pid %d fd %d, %d", networkIfPid, networkIfFd[0], networkIfFd[1] );
  } // else parent
} // forkSendfax

/**
 * drops priviledges from root - drop only temporarily to allow child processes to regain
 * if so configured
 * @param user
 * @return true on success
 * **/
bool txProc::dropPriviledge( const char* user )
{
  struct passwd* pw = getpwnam( user );
  if( pw == NULL ) 
  {
    log.error( "dropPriviledge: could not find user %s", user );
    return false;
  }
  int userId = pw->pw_uid;
  int groupId = pw->pw_gid; 
  log.info( log.LOGALWAYS, "dropPriviledge: user %s uid %d gid %d", user, userId, groupId );
  
  int result = setgid( groupId );
  if( result == -1 )
  {
    log.error( "dropPriviledge: failed on setgid - %s", strerror( errno ) );
    return false;
  }
  result = utils::dropPriviledgeTemp( userId );
  if( result != 0 )
  {
    log.error( "dropPriviledge: failed on setuid - %s", strerror( errno ) );
    return false;
  }

  return true;
} // dropPriviledge

/**
 * signal handler
 * @param signo
 * **/
void txProc::sigHandler( int signo )
{
//  fprintf( stderr, "sigHandler received signal '%s'\n", strsignal( signo ) );
  switch( signo )
  {
    case SIGINT:
      sendSignalCommand( baseEvent::CMD_SHUTDOWN );
      break;
    case SIGTERM:
      sendSignalCommand( baseEvent::CMD_SHUTDOWN );
      break;
    case SIGCHLD:
      sendSignalCommand( baseEvent::CMD_CHILD_SIGNAL );
      break;
    case SIGUSR1:
      sendSignalCommand( baseEvent::CMD_RESET_STATS );
      break;
    case SIGUSR2:
      sendSignalCommand( baseEvent::CMD_REOPEN_LOG );
      break;
    case SIGHUP:
      sendSignalCommand( baseEvent::CMD_STATS );
      break;
    case SIGALRM:
      sendSignalCommand( baseEvent::CMD_TIMER_SIGNAL );
      bChildSignal = true;  // used in recovery
      break;
    default:
      sendSignalCommand( baseEvent::CMD_NONE );
//      fprintf( stderr, "sigHandler cannot handle signal %s\n", strsignal( signo ) );
  } // switch
} // sigHandler

/**
 * rotates logs
 * **/
void txProc::rotateLogs( )
{
  if( !pOptions->bNoRotate )
  {
    recoveryLog::rotate( pOptions->logBaseDir.c_str(), pOptions->logrotatePath, pOptions->runAsUser, pOptions->logGroup, pOptions->logFilesToKeep );
  }
} // rotateLogs

/**
 * App main
 * @param argc
 * @param argv
 * **/
int main( int argc, char* argv[] )
{
  pOptions = new options( );
  bool bDone = !pOptions->parseOptions( argc, argv );
  if( bDone )   // typically the user requested version or help
  {
    delete pOptions;
    return 0;
  } //  if

  try
  {
    theServer = new txProc( argc, argv );

    // rotate logs
    theServer->rotateLogs();

    // init logging
    theServer->log.openLogfile( pOptions->logFile.c_str() );
    theServer->log.setDefaultLevel( (loggerDefs::eLogLevel)pOptions->defaultLogLevel );
    if( pOptions->bLogStderr )
      theServer->log.setLogStdErr( true );
    else if( pOptions->bLogConsole )
      theServer->log.setLogConsole( true );
    else
      theServer->log.setLogConsole( false );
    pOptions->logOptions(); 

    // daemonise if requested
    if( pOptions->bDaemon )
      theServer->runAsDaemon();
  } // try
  catch( Exception e )
  {
    delete pOptions;
    if( theServer != NULL ) delete theServer;
    return -1;
  } // catch

  // set the default umask
  umask( S_IWGRP | S_IXGRP | S_IWOTH | S_IXOTH );

  // register signal handling
  signal( SIGINT, txProc::sigHandler );
  signal( SIGTERM, txProc::sigHandler );
  signal( SIGCHLD, txProc::sigHandler );
  signal( SIGUSR1, txProc::sigHandler );
  signal( SIGUSR2, txProc::sigHandler );
  signal( SIGHUP, txProc::sigHandler );
  signal( SIGALRM, txProc::sigHandler );
  
  if( !pOptions->recoverFile.empty() )
    theServer->recover( );
  else
    theServer->main( );

  // cleanup deletes the global objects
  delete theServer;
  delete pOptions;
//  cleanup( "main" );  // pedantic shutdown and deletion 
  return 0;
} // main

