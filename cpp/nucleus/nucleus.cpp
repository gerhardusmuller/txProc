/** @class nucleus
 core to message or event based queuing and execution
 SIGTERM - to exit
 
 $Id: nucleus.cpp 3057 2014-03-26 14:35:49Z gerhardus $
 Versioning: a.b.c a is a major release, b represents changes or new features, c represents bug fixes. 
 @version 1.0.0		21/09/2009		Gerhardus Muller		Script created
 @version 1.1.0		25/11/2011		Gerhardus Muller		better error handling for recoveryLog reopen / umask change on createStatsDir
 @version 1.2.0		12/04/2011		Gerhardus Muller		remove the appendTrace, setTraceTimestamp
 @version 1.3.0		24/05/2011		Gerhardus Muller		ported to Mac
 @version 1.4.0		13/08/2012		Gerhardus Muller		named queues plus dynamic add/remove queues
 @version 1.5.0		16/08/2012		Gerhardus Muller		changed to using a socketPair for signal handling - most likely some of the child signals went missing
 @version 1.5.1		23/08/2012		Gerhardus Muller		support for a queue that handles events destined for non local queues
 @version 1.6.0		04/09/2012		Gerhardus Muller		moved the main Unix Domain listening socket into the nucleus code
 @version 1.7.0		05/06/2013		Gerhardus Muller		support for FD_CLOEXEC
 @version 1.8.0		20/06/2013		Gerhardus Muller		theNetworkIf for the fdsToRemainOpen list
 @version 1.8.1		26/03/2014		Gerhardus Muller		buildLookupMaps was forgotten in a reconfigure createqueue

 @note

 @todo
 
 @bug

	Copyright Notice
 */

#include <fstream>
#include <iostream>
#include <sstream>
#include <signal.h>
#include <unistd.h>
#include <sys/poll.h>
#include <sys/types.h> 
#include <sys/wait.h>
#include <sys/socket.h>
#include <sys/stat.h>
#ifndef PLATFORM_MAC 
#include <sys/prctl.h>
#endif
#include <errno.h>
#include <string.h>
#include <pwd.h>
#include <stddef.h>
#include <fcntl.h>

#include "nucleus/nucleus.h"
#include "nucleus/recoveryLog.h"
#include "nucleus/network.h"
#include "utils/utils.h"
#include "utils/unixSocket.h"

bool nucleus::bRunning = true;
bool nucleus::bReopenLog = false;
bool nucleus::bResetStats = false;
bool nucleus::bDump = false;
bool nucleus::bTimerTick = false;
int  nucleus::sigTermCount = 0;
int  nucleus::signalFd0 = 0;
recoveryLog* nucleus::theRecoveryLog = NULL;
const char *const nucleus::FROM = typeid( nucleus ).name();
optionsNucleus* pOptionsNucleus = NULL;

/**
 Construction
 @param nucleusFd - unix domain socket to submit events to the nucleus - the nucleus reads the [1] side
 @param theRecoveryLog
 @param bRecovery - true if it is a recovery instance
 */
nucleus::nucleus( int nucleusFd[2], int theParentFd, recoveryLog* recovery, bool bRecovery, int theArgc, char* theArgv[], int theNetworkIf )
  : object( "nucleus" )
{
  theRecoveryLog = recovery;
  bRecoveryProcess = bRecovery;
  pNetwork = NULL;
  pRecSock = NULL;
  pSignalSock = NULL;
  numQueues = 0;
  totNumWorkers = 0;
  argc = theArgc;
  argv = theArgv;
  queueDesc = NULL;
  eventSourceWriteFd = nucleusFd[0];  // needs to be inheritable / open down to worker level
  eventSourceFd = nucleusFd[1];
  parentFd = theParentFd;
  networkIfFd = theNetworkIf;

  // prevent these file handles from being available / open on all the children
  unixSocket::setCloseOnExec( true, parentFd );
  unixSocket::setCloseOnExec( true, eventSourceFd );
}	// nucleus

/**
 Destruction
 */
nucleus::~nucleus()
{
  log.generateTimestamp();
  if( pNetwork != NULL ) delete pNetwork;
  if( pRecSock != NULL ) delete pRecSock;
  if( pSignalSock != NULL ) delete pSignalSock;
  queueContainerStrMapIteratorT it;
  for( it = queues.begin(); it != queues.end(); it++ )
  {
    queueContainer* pQueue = it->second;
    log.debug( log.LOGNORMAL, "~nucleus termChildren on queue '%s'", pQueue->getQueueName().c_str() );
    pQueue->termChildren( );
  } // for

  // give the children a chance to exit and wait on them so they are not zombies
  // will also delete the child objects
  waitForChildrenToExit();

  for( it = queues.begin(); it != queues.end(); it++ )
  {
    queueContainer* pQueue = it->second;
    log.info( log.LOGNORMAL, "~nucleus about to delete queue:'%s'", pQueue->getQueueName().c_str() );
    delete pQueue;
  } // for
  queues.clear();

  if( queueDesc != NULL )
  {
    closeStatsFiles();
    delete[] queueDesc;
  } // if
  log.info( log.MIDLEVEL, "~nucleus cleaned up - %d queues left", queues.size() );
  if( pOptionsNucleus != NULL ) delete pOptionsNucleus;
}	// ~nucleus

/**
 Standard logging call - produces a generic text version of the nucleus.
 Memory allocation / deleting is handled by this nucleus.
 @return pointer to a string describing the state of the nucleus.  The string has an unspecified lifetime and is not multi-thread safe
 */
std::string nucleus::toString( )
{
  std::ostringstream oss;
  oss << " ";
	return oss.str();
}	// toString

/**
 * waits for all children to exit
 * **/
void nucleus::waitForChildrenToExit( )
{
  log.info( log.LOGALWAYS, "waitForChildrenToExit entered - waiting for ECHILD" );

  // wait for the children to exit - eventually wait returns ECHILD - no children
  int pid = 0;
  int status = 0;
  pid = wait( &status );
  while( ( pid != -1 ) || (( pid==-1)&&(errno==EINTR)) )
  {
    if( pid > 0 )
    {
      log.info( log.MIDLEVEL, "waitForChildrenToExit child pid %d exited", pid );
      queueContainerIntMapIteratorT it;
      it = workerPids.find( pid );
      if( it == workerPids.end() )
        log.warn( log.LOGALWAYS, "waitForChildrenToExit child pid %d no longer in workerPids", pid );
      else
      {
        queueContainer* pQueue = it->second;
        pQueue->respawnChild( pid, false );
        workerPids.erase( it );
      } // else
    }
    else
      log.info( log.MIDLEVEL, "waitForChildrenToExit pid %d err %s", pid, strerror( errno ) );
    pid = wait( &status );
  } // while
  log.info( log.LOGALWAYS, "waitForChildrenToExit exited" );
} // waitForChildrenToExit

/**
 * dump as http - url reporting
 * this has to run after writeStats - it generates the stats;
 * **/
void nucleus::dumpHttp( const std::string& time )
{
  if( pOptionsNucleus->statsUrl.length() > 0 )
  {
    // locate the stats queue
    log.debug( log.LOGNORMAL, "dumpHttp: statsQueue:'%s'", pOptionsNucleus->statsQueue.c_str() );
    queueContainer* pStatsQueue = findQueueByName( pOptionsNucleus->statsQueue );
    queueContainerStrMapIteratorT it;

    // base stats event for the nucleus
//    baseEvent* pEvent = new baseEvent( baseEvent::EV_URL );
//    pEvent->setUrl( pOptionsNucleus->statsUrl );
//    pEvent->addParam( "cmd", "stats" );
//    pEvent->addParam( "name", typeid(*this).name() );
//    pEvent->addParam( "time", time );
//    pEvent->addParam( "hostid", hostId ); 
//    pEvent->addParam( "recoverycount", numRecoveryEvents );
//    pEvent->addParam( "numqueues", (unsigned int)queues.size() );

//    std::string queueNames;
//    unsigned int queueNum = 0;
//    for( it = queues.begin(); it != queues.end(); it++ )
//    {
//      queueContainer* pQueue = it->second;
//      queueNames.append( pQueue->getQueueName() );
//      if( ++queueNum != queues.size()-1 ) queueNames.append( "," );
//    } // for
//    pEvent->addParam( "queuenames", queueNames );
//    pStatsQueue->submitEvent( pEvent );
//    numRecoveryEvents = 0;

    // create an event for every queue as well
    for( it = queues.begin(); it != queues.end(); it++ )
    {
      queueContainer* pQueue = it->second;

      baseEvent* pEvent = new baseEvent( baseEvent::EV_URL, pOptionsNucleus->statsQueue.c_str() );
      pEvent->setUrl( pOptionsNucleus->statsUrl );
      pEvent->addParam( "cmd", "stats" );
      pEvent->addParam( "qname", pQueue->getQueueName() );
      pEvent->addParam( "time", time );
      pEvent->addParam( "hostid", hostId ); 
      pEvent->addParam( "csv", pQueue->getStatusStr() );
      pStatsQueue->submitEvent( pEvent );
    } // for
  } // if
} // dumpHttp

/**
 * init - creates socket and required functions
 * **/
void nucleus::init( )
{
  bExitOnDone = false;
  numRecoveryEvents = 0;
  now = time( NULL );
  pOptionsNucleus = new optionsNucleus( );
  bool bDone = !pOptionsNucleus->parseOptions( argc, argv );
  if( bDone )
  {
    delete pOptionsNucleus;
    return;
  } // if( bDone

  // seed the random number generator
  srandom( time(NULL) );
  
  // configure logging
  log.openLogfile( pOptionsNucleus->logFile.c_str(), pOptionsNucleus->bFlushLogs );
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
  baseEvent::staticLogger.init( "baseEvent", (loggerDefs::eLogLevel)pOptionsNucleus->defaultLogLevel );
  baseEvent::staticLogger.setAddPid( true );
  pOptionsNucleus->logOptions(); 
  
  // drop executing rights to a non-priviledged user
  bool bResult = dropPriviledge( pOptionsNucleus->runAsUser.c_str() );
  if( !bResult )
  {
    close( eventSourceFd );  // prevent the attempted delivery of events
    throw Exception( log, log.ERROR, "init: failed to drop priviledges to user %s\n", pOptionsNucleus->runAsUser.c_str() );
  }

  // register signal handling
  signal( SIGINT, SIG_IGN );
  signal( SIGTERM, nucleus::sigHandler );
  signal( SIGCHLD, nucleus::sigHandler );
  signal( SIGALRM, nucleus::sigHandler );

  // reset the signal blocking mask that we inherited
  sigset_t blockmask;
  sigset_t oldmask;
  sigemptyset( &blockmask );    // unblock all the signals
  if( sigprocmask( SIG_SETMASK, &blockmask, &oldmask ) < 0 )
    throw Exception( log, log.ERROR, "init: sigprocmask returned -1" );

  // retrieve our hostname
  char hostname[256];
  hostname[0] = '\0';
  gethostname( hostname, 255 );
  hostId = hostname;

  // create the networking object that will handle events from outside and our children
  pNetwork = new network();

  // create the queues
  createQueues();

  // create an internal socket pair which we use to feed signal events to the main process
  unixSocket::createSocketPair( signalFd, "nucleus signal socket pair" );
  signalFd0 = signalFd[0];
  pSignalSock = new unixSocket( signalFd[1], unixSocket::ET_SIGNAL, false, "signalFd1" );
  pSignalSock->setNonblocking();

  // create the receive socket object - from where our instructions originate
  pRecSock = new unixSocket( eventSourceFd, unixSocket::ET_QUEUE_EVENT, false, "nucleusFd1" );
  pRecSock->setNonblocking( );

  buildLookupMaps();
} // init

/**
 * retrieve the parsed queue definitions from the options file
 * in the section [queueN] look for the parameters
 * name(required), type(optional), numWorkers (optional), maxLength (optional), maxExecTime (optional), persistentApp (optional)
 * types can be 'straight', 'batch'
 * **/
void nucleus::createQueues( )
{
  queueDesc = new tQueueDescriptor[pOptionsNucleus->maxNumQueues];
  numQueues = 0;

  if( pOptionsNucleus->activeQueues.length() > 0 )
  {
    std::size_t startPos = 0;
    std::size_t endPos = pOptionsNucleus->activeQueues.find_first_of( ',', startPos );

    log.info( log.LOGMOSTLY, "createQueues creating: %s", pOptionsNucleus->activeQueues.c_str() );
    while( (numQueues < pOptionsNucleus->maxNumQueues) && (startPos != std::string::npos) )
    {
      std::string q = pOptionsNucleus->activeQueues.substr( startPos, endPos-startPos );

      // create the required queues
      createQueue( q );

      startPos = endPos;
      if( startPos != std::string::npos )
      {
        startPos++;
        endPos = pOptionsNucleus->activeQueues.find_first_of( ',', startPos );
      } // if
    } // while
  } // if
  else
    log.warn( log.LOGALWAYS, "createQueues: no queues - please configure activeQueues" );

  openStatsFiles( 0, numQueues );
} // createQueues

/**
 * creates a single queue instance
 * @return true on success
 * **/
bool nucleus::createQueue( const std::string& q )
{
  if( numQueues >= pOptionsNucleus->maxNumQueues )
  {
    log.warn( log.LOGALWAYS, "createQueue: cannot create:%s no space left", q.c_str() );
    return false;
  } // if

  char tmp[64];
  sprintf( tmp, "queues.%s.", q.c_str() );
  std::string baseName( tmp );
  std::string key( baseName ); key.append( "name" );

  if( pOptionsNucleus->existVar(key.c_str()) )
  {
    pOptionsNucleus->getAsString( key.c_str(), queueDesc[numQueues].name );
    queueDesc[numQueues].key = baseName;
    key.assign( baseName ); key.append( "type" );
    pOptionsNucleus->getAsString( key.c_str(), queueDesc[numQueues].type );
    if( queueDesc[numQueues].type.empty() ) queueDesc[numQueues].type = "straight";
    queueDesc[numQueues].statsFd = -1;
    // replies on a stream connection go back via the networkIf straight from the worker
    queueDesc[numQueues].fdsToRemainOpen.push_back( networkIfFd );

    log.info( log.LOGALWAYS, "createQueue: queue:%s name:'%s' type:'%s'", key.c_str(), queueDesc[numQueues].name.c_str(), queueDesc[numQueues].type.c_str() );
    queueDesc[numQueues].pQueue = new queueContainer( &queueDesc[numQueues], theRecoveryLog, bRecoveryProcess, eventSourceWriteFd );
    queues.insert( queueContainerStrPairT(queueDesc[numQueues].name,queueDesc[numQueues].pQueue) );
    totNumWorkers += queueDesc[numQueues].numWorkers;

    // create the stats directory if it does not exist
    createStatsDir( numQueues );
    
    numQueues++;
    return true;
  } // if existVar
  else
  {
    log.warn( log.LOGALWAYS, "createQueue: failed to instantiate queue:'%s'", key.c_str() );
    return false;
  } // else
} // createQueue

/**
 * drops a queue and rebuilds queueDesc array
 * not fantastic efficiency but then this should really happen seldom
 * the user should shutdown all workers beforehand - we assume we have a fully operational system and 
 * wont hang around waiting for children exiting nicely
 * @param this has to be the nucleus.xxx.name of the queue - normally name and xxx would be the same
 * @return true on success
 * **/
void nucleus::dropQueue( const std::string& q )
{
  tQueueDescriptor* newQueueDesc = new tQueueDescriptor[pOptionsNucleus->maxNumQueues];
  int numNewQueues = 0;
  int numQueuesDropped = 0;

  for( unsigned int i = 0; i < numQueues; i++ )
  {
    if( q.compare(queueDesc[i].name) != 0 )
    {
      newQueueDesc[numNewQueues].name = queueDesc[i].name;
      newQueueDesc[numNewQueues].type = queueDesc[i].type;
      newQueueDesc[numNewQueues].key = queueDesc[i].key;
      newQueueDesc[numNewQueues].numWorkers = queueDesc[i].numWorkers;
      newQueueDesc[numNewQueues].maxLength = queueDesc[i].maxLength;
      newQueueDesc[numNewQueues].maxExecTime = queueDesc[i].maxExecTime;
      newQueueDesc[numNewQueues].parseResponseForObject = queueDesc[i].parseResponseForObject;
      newQueueDesc[numNewQueues].bRunPriviledged = queueDesc[i].bRunPriviledged;
      newQueueDesc[numNewQueues].bBlockingWorkerSocket = queueDesc[i].bBlockingWorkerSocket;
      newQueueDesc[numNewQueues].persistentApp = queueDesc[i].persistentApp;
      newQueueDesc[numNewQueues].errorQueue = queueDesc[i].errorQueue;
      newQueueDesc[numNewQueues].pQueue = queueDesc[i].pQueue;
      newQueueDesc[numNewQueues].statsFile = queueDesc[i].statsFile;
      newQueueDesc[numNewQueues].statsDir = queueDesc[i].statsDir;
      newQueueDesc[numNewQueues].statsFd = queueDesc[i].statsFd;
      numNewQueues++;
    } // if
    else
    {
      // it is the queue we need to delete
      queueContainerStrMapIteratorT it = queues.find( q );
      if( it != queues.end() ) queues.erase( it );
      if( queueDesc[i].pQueue == NULL )
      {
        if( queueDesc[i].pQueue->getTotalWorkers() > 0 ) log.warn( log.LOGALWAYS, "dropQueue queue:%s workers should be 0 at this point! - a total of %d workers will be aborted", q.c_str(), queueDesc[i].pQueue->getTotalWorkers() );
        // strictly speaking the user should have already killed off the children
        // send a SIGTERM to any remaining children
        queueDesc[i].pQueue->shutdown();  
        // immediatly follow by a SIGKILL
        queueDesc[i].pQueue->shutdown();
        delete queueDesc[i].pQueue;
      } // if
      if( queueDesc[i].statsFd != -1 ) close( queueDesc[i].statsFd );
      numQueuesDropped++;
    } // else
  } // for

  delete[] queueDesc;
  queueDesc = newQueueDesc;
  numQueues = numNewQueues;
  buildLookupMaps();          // rebuild maps
  log.info( log.LOGALWAYS, "dropQueue: tried to drop queue:'%s' dropped %d queues, total of %d queues left", q.c_str(), numQueuesDropped, numQueues );
} // dropQueue

/**
 * insert all the workers from all the queues into the poll array and create 
 * maps that can be used as a lookup from file id or pid to queue object
 * with epoll we do not any longer need to rebuild the entire map - for the moment we leave it as is
 * **/
void nucleus::buildLookupMaps( )
{
  // clean the existing maps
  workerFds.clear();
  workerPids.clear();
  pNetwork->resetRdPollMap();

  // init the poll array with static entries
  pNetwork->buildRdPollMap();
  pNetwork->addRdFd( pRecSock );
  pNetwork->addRdFd( pSignalSock );

  for( queueContainerStrMapIteratorT it = queues.begin(); it != queues.end(); it++ )
  {
    queueContainer* pQueue = it->second;
    std::string fdList = pQueue->getQueueName();
    std::string pidList = pQueue->getQueueName();
    fdList.append( ": " );
    pidList.append( ": " );

    int pid;
    unixSocket* pSocket;
    pQueue->resetItFd();
    for( int fd = pQueue->getNextFd(pid,pSocket); fd != -1; fd = pQueue->getNextFd(pid,pSocket) )
    {
      pNetwork->addRdFd( pSocket );
      workerFds.insert( queueContainerIntPairT( fd, pQueue ) );
      workerPids.insert( queueContainerIntPairT( pid, pQueue ) );

      char str[32];
      sprintf( str, "%d,", fd );
      fdList.append( str );
      sprintf( str, "%d,", pid );
      pidList.append( str );
    } // for
    log.info( log.LOGALWAYS, "buildLookupMaps: fds %s pids %s", fdList.c_str(), pidList.c_str() );
  } // for
} // buildLookupMaps

/**
 * respawns a child that exited
 * if bRunning is false we only clean up the workerPids map, not the workerFds map
 * handle workers that are allowed to die off to adjust the number of workers accordingly
 * @exception on error
 * **/
void nucleus::respawnChild( )
{
  int pid = 0;
  int status = 0;
  
  // multiple children may have exited
  while( (pid = waitpid( -1, &status, WNOHANG ) ) > 0 )
  {
    try
    {
      // find the queue that owns the dead worker
      queueContainerIntMapIteratorT it;
      it = workerPids.find( pid );
      if( it == workerPids.end() ) throw Exception( log, loggerDefs::ERROR, "respawnChild: pid %d not found", pid );
      queueContainer* pQueue = it->second;
      if( pQueue == NULL ) throw Exception( log, loggerDefs::ERROR, "respawnChild: pid %d pQueue is NULL", pid );
      workerPids.erase( it );

      // auto restart - remove stale entry from workers
      if( bRunning )
      {
        int newPid = pQueue->respawnChild( pid, true );
        if( newPid == 0 ) throw Exception( log, loggerDefs::ERROR, "respawnChild: pid %d did not belong to retrieved queue", pid );
        if( newPid == -1 )  // terminal - remove completely and recalc pollFd
        {
          log.info( log.LOGALWAYS, "respawnChild: terminal child pid %d exit status:'%s'", pid, utils::printExitStatus(status).c_str() );
          buildLookupMaps();
        }
        else
        {
          workerPids.insert( queueContainerIntPairT( newPid, pQueue ) );
          log.info( log.LOGALWAYS, "respawnChild: child pid %d newpid %d exit status:'%s'", pid, newPid, utils::printExitStatus(status).c_str() );
        } // else
      } // if
      else
      {
        log.info( log.LOGALWAYS, "respawnChild: oldPid %d exit status:'%s'", pid, utils::printExitStatus(status).c_str() );
        pQueue->respawnChild( pid, false );
      } // else
    } // try
    catch( Exception e )
    {
    } // catch
  } // while
} // respawnChild

/**
 * signal all children
 * @param sig - the signal to send
 * **/
void nucleus::signalChildren( int sig )
{
  queueContainerStrMapIteratorT it;
  for( it = queues.begin(); it != queues.end(); it++ )
  {
    queueContainer* pQueue = it->second;
    pQueue->signalChildren( sig );
  } // for
} // signalChildren

/**
 * sends command requests to the children
 * @param command - the command to send
 * **/
void nucleus::sendCommandToChildren( baseEvent::eCommandType command )
{
  baseEvent* pCommand = new baseEvent( baseEvent::EV_COMMAND );
  pCommand->setCommand( command );
  queueContainerStrMapIteratorT it;
  for( it = queues.begin(); it != queues.end(); it++ )
  {
    queueContainer* pQueue = it->second;
    pQueue->sendCommandToChildren( pCommand );
  } // for

  delete pCommand;
} // sendCommandToChildren

/**
 * sends command requests to the children
 * if a queue is specified restrict it to the appropriate queue
 * @param pCommand - the command to send
 * @exception on an invalid queuename specified
 * **/
void nucleus::sendCommandToChildren( baseEvent* pCommand )
{
  // sub queues are not supported for the moment
  std::string queueName = pCommand->getFullDestQueue();
  if( (queueName.compare("default")==0) || queueName.empty() )
  {
    queueContainerStrMapIteratorT it;
    for( it = queues.begin(); it != queues.end(); it++ )
    {
      queueContainer* pQueue = it->second;
      pQueue->sendCommandToChildren( pCommand );
    } // for
  } // if
  else
  {
    // only send the command to the appropriate children
    queueContainer* pQueue = findQueueByName( queueName );
    pQueue->sendCommandToChildren( pCommand );
  } // else
} // sendCommandToChildren

/**
 * send workers hosting a persistent app a CMD_EXIT_WHEN_DONE to allow them
 * append a CMD_END_OF_QUEUE 
 * to shutdown orderly
 * **/
void nucleus::exitWhenDone( )
{
  queueContainerStrMapIteratorT it;
  for( it = queues.begin(); it != queues.end(); it++ )
  {
    queueContainer* pQueue = it->second;
    if( pQueue->isPersistentApp() )
      pQueue->exitWhenDone();
  } // for

} // exitWhenDone

/**
 * find the appropriate queue by name
 * @param queueName
 * @param bThrow - true to throw otherwise a NULL is returned
 * @return pointer to the queueContainer
 * @exception on not found
 * **/
queueContainer* nucleus::findQueueByName( const std::string& queueName, bool bThrow )
{
  queueContainerStrMapIteratorT it = queues.find( queueName );
  if( it == queues.end() )
  {
    if( bThrow ) throw Exception( log, log.WARN, "findQueueByName: queue '%s' not found", queueName.c_str() );
    return NULL;
  } // if

  queueContainer* pQueue = it->second;
  if( pQueue == NULL )
    throw Exception( log, log.ERROR, "findQueueByName: pQueue for '%s' is NULL", queueName.c_str() );

  return pQueue;
} // findQueueByName

/**
 * handles a reconfigure command
 * **/
void nucleus::reconfigure( baseEvent* pCommand )
{
  try
  {
    log.info( log.LOGMOSTLY ) << "reconfigure: received command '" << pCommand->toString() << "'";
    std::string cmd = pCommand->getParam( "cmd" );
    if( (cmd.length()==0) )
      throw Exception( log, log.WARN, "reconfigure: require parameter cmd" );

    if( cmd.compare( "updateworkers" ) == 0 )
    {
      std::string queue = pCommand->getParam( "queue" );
      std::string val = pCommand->getParam( "val" );
      if( (queue.length()==0)||(val.length()==0) )
        throw Exception( log, log.WARN, "reconfigure: require parameters queue,val for command '%s'", cmd.c_str() );

      queueContainer* pQueue = findQueueByName( queue );
      int newNum = atoi( val.c_str() );
      log.info( log.LOGALWAYS, "reconfigure: updateworkers on queue '%s' to %d", queue.c_str(), newNum );
      int numNewWorkers = pQueue->resizeWorkerPool( newNum );
      buildLookupMaps();

      for( int i = 0; i < numNewWorkers; i++ )
        pQueue->feedWorker();
    } // if( cmd.compare
    else if( cmd.compare( "updatemaxqueuelen" ) == 0 )
    {
      std::string queue = pCommand->getParam( "queue" );
      std::string val = pCommand->getParam( "val" );
      if( (queue.length()==0)||(val.length()==0) )
        throw Exception( log, log.WARN, "reconfigure: require parameters queue,val for command '%s'", cmd.c_str() );

      queueContainer* pQueue = findQueueByName( queue );
      int newNum = atoi( val.c_str() );
      pQueue->setMaxQueueLen( newNum );
    } // if( cmd.compare
    else if( cmd.compare( "updatemaxexectime" ) == 0 )
    {
      std::string queue = pCommand->getParam( "queue" );
      std::string val = pCommand->getParam( "val" );
      if( (queue.length()==0)||(val.length()==0) )
        throw Exception( log, log.WARN, "reconfigure: require parameters queue,val for command '%s'", cmd.c_str() );

      queueContainer* pQueue = findQueueByName( queue );
      unsigned int newNum = atoi( val.c_str() );
      pQueue->setMaxExecTime( newNum );
    } // if( cmd.compare
    else if( cmd.compare( "loglevel" ) == 0 )
    {
      std::string val = pCommand->getParam( "val" );
      if( val.length()==0 )
        throw Exception( log, log.WARN, "reconfigure: require parameter val for command '%s'", cmd.c_str() );

      int newLevel = atoi( val.c_str() );
      log.info( log.LOGALWAYS, "reconfigure: setting loglevel to %d", newLevel );
      pOptionsNucleus->defaultLogLevel = newLevel;
      log.setDefaultLevel( (loggerDefs::eLogLevel) pOptionsNucleus->defaultLogLevel );
    } // if( cmd.compare
    else if( cmd.compare( "freeze" ) == 0 )
    {
      std::string queue = pCommand->getParam( "queue" );
      std::string val = pCommand->getParam( "val" );
      if( (queue.length()==0)||(val.length()==0) )
        throw Exception( log, log.WARN, "reconfigure: require parameters queue,val for command '%s'", cmd.c_str() );

      queueContainer* pQueue = findQueueByName( queue );
      bool bFreeze = (bool)atoi( val.c_str() );
      pQueue->freeze( bFreeze );
    } // if( cmd.compare
    else if( cmd.compare( "createqueue" ) == 0 )
    {
      // pick up any new queue definitions
      pOptionsNucleus->parseOptions( argc, argv );
      std::string q = pCommand->getParam( "queue" );
      std::string val = pCommand->getParam( "val" );
      if( (q.length()==0)||(val.length()==0) )
        throw Exception( log, log.WARN, "reconfigure: require parameters queue,val for command '%s'", cmd.c_str() );

      bool bCreate = (bool)atoi( val.c_str() );
      if( bCreate )
      {
        bool bCreated = createQueue( q );
        if( bCreated )
        {
          openStatsFiles( numQueues-1, 1 );
          buildLookupMaps();          // rebuild maps
        } // if
      } // if
      else
      {
        dropQueue( q );
      } // else
    } // if( cmd.compare
    else
      log.warn( log.LOGALWAYS, "reconfigure: cmd '$s' not supported", cmd.c_str() );
  } // try
  catch( Exception e )
  {
  } // catch
} // reconfigure

/**
 * handles a reconfigure command for workers
 * @param pCommand
 * **/
void nucleus::workerReconfigure( baseEvent* pCommand )
{
  try
  {
    log.info( log.LOGMOSTLY ) << "workerReconfigure: received command '" << pCommand->toString() << "'";
    std::string queue = pCommand->getParam( "queue" );
    if( (queue.length()==0) )
      throw Exception( log, log.WARN, "workerReconfigure: require parameter 'queue'" );

    queueContainer* pQueue = findQueueByName( queue );
    pQueue->reconfigureCmd( pCommand );
  } // try
  catch( Exception e )
  {
  } // catch
} // workerReconfigure

/**
 * queue a new event
 * @param pEvent
 * **/
void nucleus::queueEvent( baseEvent* pEvent )
{
  std::string& destQueue = pEvent->getDestQueue();
  log.info( log.LOGNORMAL, "queueEvent: to queue '%s'", destQueue.c_str() );

  try
  {
    queueContainer* pQueue = findQueueByName( destQueue, false );
    if( pQueue == NULL ) pQueue = routeNonLocalqueue( destQueue );
    pQueue->submitEvent( pEvent );
  } // try
  catch( Exception e )
  {
    char reason[256];
    snprintf( reason, 255, "queue name '%s' not available", destQueue.c_str() );
    reason[255] = '\0';
    theRecoveryLog->writeEntry( pEvent, reason, FROM, "new " );
    numRecoveryEvents++;
    sendResult( pEvent, false, std::string(), std::string(), std::string(), std::string(reason) );
    log.error() << "queueEvent: '" << reason << "' for event:" << pEvent->toString();
  } // catch
} // queueEvent

/**
 * find the queue to route non local queues with 
 * @param pEvent
 * **/
queueContainer* nucleus::routeNonLocalqueue( const std::string& destQueue )
{
  if( pOptionsNucleus->notLocalqueueRouterQueue.empty() )
    throw Exception( log, log.WARN, "routeNonLocalqueue: queue '%s' not found", destQueue.c_str() );
  return findQueueByName( pOptionsNucleus->notLocalqueueRouterQueue );
} // routeNonLocalqueue

/**
 * dumps the list to the recovery log
 * expired events are not dumped
 * @param reason
 * **/
void nucleus::dumpLists( const char* reason )
{
  queueContainerStrMapIteratorT it;
  for( it = queues.begin(); it != queues.end(); it++ )
  {
    queueContainer* pQueue = it->second;
    pQueue->dumpQueue( reason );
  } // for
} // dumpLists

/**
 * send a result message - it is not regarded as an error if there is no returnFd specified
 * do not send result messages if it is a recovery process - the fd in the event is not
 * valid - will later change this to be an index into an array that can be setup correctly
 * even for a recovery process
 * @param pEvent - the source event
 * @param bSuccess - result of the execution
 * @param result - string result
 * **/
void nucleus::sendResult( baseEvent* pEvent, bool bSuccess, const std::string& result, const std::string& errorString, const std::string& traceTimestamp, const std::string& failureCause, const std::string& systemParam )
{
  int returnFd = pEvent->getReturnFd( );
  if( ( returnFd != -1 ) && !bRecoveryProcess && !pEvent->hasBeenExpired())
  {
    pEvent->shiftReturnFd();  // drop the return fd that we have just used
    baseEvent* pReturn = new baseEvent( baseEvent::EV_RESULT );
    pReturn->setSuccess( bSuccess );
    if( !result.empty() ) pReturn->setResult( result );
    pReturn->setRef( pEvent->getRef() );
    if( !errorString.empty() ) pReturn->setErrorString( errorString );
    if( !traceTimestamp.empty() ) pReturn->setTraceTimestamp( traceTimestamp );
    if( !failureCause.empty() ) pReturn->setFailureCause( failureCause );
    if( !systemParam.empty() ) pReturn->setSystemParam( systemParam );
    pReturn->setTrace( pEvent->getTrace() );
    pReturn->setReturnFd( pEvent->getFullReturnFd() );
    
    pReturn->serialise( returnFd );
    delete pReturn;
  } // if
} // sendResult

/**
 * scan lists for expired events and expire these
 * **/
void nucleus::scanForExpiredEvents( )
{
  queueContainerStrMapIteratorT it;
  for( it = queues.begin(); it != queues.end(); it++ )
  {
    queueContainer* pQueue = it->second;
    pQueue->scanForExpiredEvents( );
  } // for
} // scanForExpiredEvents

/** 
 * check if any workers are overrunning execution time
 * **/
void nucleus::checkOverrunningWorkers( )
{
  queueContainerStrMapIteratorT it;
  for( it = queues.begin(); it != queues.end(); it++ )
  {
    queueContainer* pQueue = it->second;
    pQueue->checkOverrunningWorkers();
  } // for
} // checkOverrunningWorkers

/**
 * main nucleus processing loop
 * **/
void nucleus::main( )
{
  baseEvent::theRecoveryLog = theRecoveryLog;   // separate process - need to re-init
  
  // set an alarm for maintenance events
  now = time( NULL );
  if( pOptionsNucleus->maintInterval > 0 )
    alarm( pOptionsNucleus->maintInterval );
  else
    log.warn( log.LOGALWAYS, "main: - not running a maintenance timer" );
  nextExpiredEventCheck = now + pOptionsNucleus->expiredEventInterval;
  
  bRunning = true;
  while( bRunning )
  {
    try
    {
      // waitForRdEvent only returns when there is an event ready
      // or when interrupted by a signal (timer as an example)
      int numReady = pNetwork->waitForRdEvent();
      log.generateTimestamp();

      // retrieve the current time and update the time for all the queues
      now = log.getNow();
      queueContainerStrMapIteratorT it1;
      for( it1 = queues.begin(); it1 != queues.end(); it1++ )
      {
        queueContainer* pQueue = it1->second;
        pQueue->setTime( now );
        if( bTimerTick ) pQueue->maintenance();
      } // for

      // only execute if multiFdWaitForEvent returned with an event - otherwise
      // it was most likely a timer or child signal
      if( numReady > 0 )
      {
        // retrieve the available file descriptors and process the events
        for( int i=0; i<numReady; i++ )
        {
          unsigned int events;
          bool bSocketError;
          unixSocket* pSocket = pNetwork->getReadSocket( i, bSocketError, events );
          if( pSocket == NULL ) throw Exception( log, log.ERROR, "main getReadSocket i:%d returned NULL pSocket", i );
          log.generateTimestamp();
          log.info( log.LOGNORMAL, "main: fd:%d type:%s bErr:%d has data", pSocket->getSocketFd(), unixSocket::eEventTypeToStr(pSocket->getEventType()), bSocketError );

          if( bSocketError )
          {
            // typically attempt to close the socket - assume it is a stream socket
            pNetwork->closeStreamSocket( pSocket );
          } // if
          else
          {
            switch( pSocket->getEventType() )
            {
              case unixSocket::ET_QUEUE_EVENT:
                {
                  // service an event from the outside
                  log.info( log.HIGHLEVEL, "main: about to unserialise ET_QUEUE_EVENT" );
                  // the event will be deleted at the point of being consumed
                  baseEvent* pEvent = baseEvent::unSerialise( pSocket );
                  while( pEvent != NULL )
                  {
                    // generate a structured reference if the event does not have a reference
                    std::string& eventRef = pEvent->getRef();
                    if( eventRef.empty() ) pEvent->generateRef();

                    // update the logging string to reflect the event reference
                    // log.updateReference( eventRef );

                    if( log.wouldLog( log.HIGHLEVEL ) ) log.debug( log.HIGHLEVEL ) << "main: unserialised:" << pEvent->toString();
                    if( pEvent->getType() == baseEvent::EV_COMMAND )
                    {
                      // execute command requested - rather handle all commands out of band and distribute them to all the workers if not handled here directly
                      //if( (pEvent->getCommand()==baseEvent::CMD_APP) || (pEvent->getCommand()==baseEvent::CMD_PERSISTENT_APP) )
                      //{ // queue events for the associated app
                      //  queueEvent( pEvent ); 
                      //} // if
                      //else if( pEvent->getCommand() == baseEvent::CMD_STATS ) 
                      if( pEvent->getCommand() == baseEvent::CMD_STATS ) 
                      {
                        std::string time = pEvent->getParam( "time" );
                        log.info( log.LOGNORMAL, "main: baseEvent::received a CMD_STATS" );
                        writeStats( time );
                        dumpHttp( time );
                        sendCommandToChildren( pEvent );
                        delete pEvent;
                      } // if
                      else if(pEvent ->getCommand() == baseEvent::CMD_RESET_STATS )
                      {
                        log.info( log.MIDLEVEL, "main: baseEvent::CMD_RESET_STATS" );
                        numRecoveryEvents = 0;
                        theRecoveryLog->resetCountRecoveryLines( );
                        queueContainerStrMapIteratorT it;
                        for( it = queues.begin(); it != queues.end(); it++ )
                        {
                          queueContainer* pQueue = it->second;
                          pQueue->resetStats( );
                        } // for
                        delete pEvent;
                      } // if
                      else if( pEvent->getCommand() == baseEvent::CMD_REOPEN_LOG )
                      {
                        log.info( log.LOGALWAYS, "main: baseEvent::CMD_REOPEN_LOG" );
                        log.reopenLogfile();

                        // open the queue specific log files
                        for( unsigned int i = 0; i < numQueues; i++ )
                        {
                          if( queueDesc[i].pQueue == NULL ) throw Exception( log, log.ERROR, "main CMD_REOPEN_LOG: queueDesc[i].pQueue == NULL" );
                          queueDesc[i].pQueue->reopenLogfile();
                        } // for

                        try
                        {
                          theRecoveryLog->reOpen();
                        } // try
                        catch( Exception e )
                        {
                          // already logged - should really something about it - the question is what?
                        } // catch
                        sendCommandToChildren( pEvent );
                        closeStatsFiles();
                        openStatsFiles( 0, numQueues );
                        delete pEvent;
                      } // if
                      else if( pEvent->getCommand() == baseEvent::CMD_NUCLEUS_CONF )
                      {
                        log.info( log.LOGALWAYS, "main: baseEvent::CMD_NUCLEUS_CONF" );
                        reconfigure( pEvent );
                        delete pEvent;
                      } // if
                      else if( pEvent->getCommand() == baseEvent::CMD_WORKER_CONF )
                      {
                        log.info( log.LOGALWAYS, "main: baseEvent::CMD_WORKER_CONF" );
                        workerReconfigure( pEvent );
                        delete pEvent;
                      } // if
                      else if( pEvent->getCommand() == baseEvent::CMD_EXIT_WHEN_DONE )
                      {
                        log.info( log.LOGALWAYS, "main: baseEvent::CMD_EXIT_WHEN_DONE" );
                        exitWhenDone();  // give persistent workers the opportunity to exit on their own
                        bExitOnDone = true;
                        delete pEvent;
                      } // if
                      else if( pEvent->getCommand() == baseEvent::CMD_SHUTDOWN )
                      {
                        log.info( log.LOGALWAYS, "main: baseEvent::CMD_SHUTDOWN" );
                        bRunning = false;
                        delete pEvent;
                      } // if
                      else
                      {
                        log.info( log.LOGALWAYS, "main: passed command %s to children", pEvent->commandToString() );
                        sendCommandToChildren( pEvent );
                        delete pEvent;
                      } // else
                    } // if EV_COMMAND
                    else
                    {
                      // normal dispatch events to be processed
                      queueEvent( pEvent );
                    } // else

                    log.info( log.LOGNORMAL, "main: done with event processing" );
                    pEvent = NULL;
                    log.generateTimestamp();
                    pEvent = baseEvent::unSerialise( pSocket );
                  } // while( pEvent != NULL
                } // case unixSocket::ET_QUEUE_EVENT
                break;
              case unixSocket::ET_SIGNAL:
                {
                  // service a signal event
                  baseEvent::eCommandType theCommand = baseEvent::CMD_NONE;
                  int bytesReceived = pSocket->read( (char*)(&theCommand), sizeof(theCommand) );
                  if( (bytesReceived==sizeof(theCommand)) && (theCommand==baseEvent::CMD_CHILD_SIGNAL) )
                    respawnChild();
                  else
                    log.warn( log.LOGALWAYS, "main: signalFd[1] not recognising command:%d", (int)theCommand );
                } // case unixSocket::ET_SIGNAL:
                break;
              case unixSocket::ET_WORKER_RET:
                {
                  // service a return from one of the children
                  // normally we only put it back onto the idle queue
                  // and feed the next worker an event from the queue if any
                  // only ever expect a single event back per child
                  queueContainerIntMapIteratorT it;
                  int fd = pSocket->getSocketFd();
                  it = workerFds.find( fd );
                  if( it != workerFds.end( ) )
                  {
                    queueContainer* pQueue = it->second;
                    baseEvent* pEvent = baseEvent::unSerialise( pSocket );
                    pQueue->releaseWorker( fd, pEvent );
                    if( pEvent != NULL ) delete pEvent;
                  } // if( it != workerFds.end
                  else
                    log.error( "main: fd:%d is not in workerFds", fd );
                } // case unixSocket::ET_SIGNAL
                break;
              case unixSocket::ET_LISTEN:
                {
                  // service listening socket event
                  pNetwork->listenEvent();
                } // case unixSocket::ET_SIGNAL:
                break;
              default:
                {
                  log.error( "main: not supporting eEventType:%s", unixSocket::eEventTypeToStr(pSocket->getEventType()) );
                } // default
            } // switch getEventType()
          } // else bSocketError
        } // for int i
      } // if( numReady
      
      log.generateTimestamp(); // want a different log timestamp for maintenance events
      if( bTimerTick )
      {
        log.info( log.LOGONOCCASION, "main: bTimerTick" );
        bTimerTick = false;
        if( (pOptionsNucleus->expiredEventInterval > 0) && (now >= nextExpiredEventCheck) )
        {
          scanForExpiredEvents();
          nextExpiredEventCheck = now + pOptionsNucleus->expiredEventInterval;
        } // if
        checkOverrunningWorkers();

        if( pOptionsNucleus->bLogQueueStatus )
        {
          queueContainerStrMapIteratorT it1;
          for( it1 = queues.begin(); it1 != queues.end(); it1++ )
          {
            queueContainer* pQueue = it1->second;
            pQueue->getStatus( true );
          } // for
        } // if

        alarm( pOptionsNucleus->maintInterval );
      } // if( bTimerTick
      if( bExitOnDone )
      {
        bool bDone = true;
        queueContainerStrMapIteratorT it;
        for( it = queues.begin(); it != queues.end(); it++ )
        {
          // assume all persistent queues have been signalled so we don't do that again
          queueContainer* pQueue = it->second;
          if( pQueue->isIdle() && !pQueue->isShutdown() && !pQueue->isExitWhenDone() )
            pQueue->shutdown();
          if( !pQueue->isIdle() )
          {
            // add some coercion if the user keeps on shutting us down
            if( sigTermCount > 1 )
            {
              log.info( log.LOGALWAYS, "main: bExitOnDone queue:'%s' still busy (%s); forcing:%d", pQueue->getQueueName().c_str(), pQueue->getStatus().c_str(), sigTermCount-1 );
              pQueue->shutdown();
            } // if
            else
              log.info( log.LOGALWAYS, "main: bExitOnDone queue:'%s' still busy (%s)", pQueue->getQueueName().c_str(), pQueue->getStatus().c_str() );
            bDone = false;
          } // if
        } // for

        if( bDone )
        {
          log.info( log.LOGALWAYS, "main: bExitOnDone and all queues empty" );
          bRunning = false;
        } // if
      } // if( bExitOnDone
    } // try
    catch( Exception e )
    {
      log.error( "main: caught exception:'%s'", e.getMessage() );
    } // catch
    catch( std::runtime_error e )
    { // json-cpp throws runtime_error
      log.error( "main: caught std::runtime_error:'%s'", e.what() );
    } // catch
  } // while
  
  // dump any entries in the queues for recovery
  dumpLists( "shutdown" );
  log.info( log.LOGALWAYS, "main: exit" );
} // main

/**
 * writes queue stats in CSV files
 * **/
void nucleus::writeStats( const std::string& time )
{
  if( queueDesc == NULL ) return;
  for( unsigned int i = 0; i < numQueues; i++ )
  {
    if( queueDesc[i].pQueue == NULL ) throw Exception( log, log.ERROR, "writeStats: queueDesc[i].pQueue == NULL" );
    if( queueDesc[i].statsFd != -1 )
    {
      std::string queueStatus = time;
      queueStatus.append( "," );
      queueStatus.append( queueDesc[i].pQueue->getStatus() );
      queueStatus.append( "\n" );
      int retVal = write( queueDesc[i].statsFd, queueStatus.c_str(), queueStatus.length() );
      if( retVal == -1 ) log.warn( log.LOGMOSTLY, "writeStats: failed to write to file:'%s%s' - %s", queueDesc[i].statsDir.c_str(),queueDesc[i].statsFile.c_str(), strerror(errno) );
    } // if
    else
      log.warn( log.LOGMOSTLY, "writeStats: failed to write to file:'%s%s' - file not open", queueDesc[i].statsDir.c_str(),queueDesc[i].statsFile.c_str() );
  } // for
} // writeStats

/**
 * closes the stats files
 * **/
void nucleus::closeStatsFiles( )
{
  if( queueDesc == NULL ) return;

  for( unsigned int i = 0; i < numQueues; i++ )
  {
    if( queueDesc[i].statsFd != -1 )
    {
      close( queueDesc[i].statsFd );
      queueDesc[i].statsFd = -1;
    } // if
  } // for
} // closeStatsFiles

/**
 * opens the stats files
 * assumes all file descriptors are closed
 * writes a header if the file must be created
 * **/
void nucleus::openStatsFiles( int startI, int num )
{
  if( queueDesc == NULL ) return;

  // generate the datestamp part of the name
  char dateStamp[64];
  struct tm *tmp;
  time_t t = now;
  tmp = localtime( &t );
  strftime( dateStamp, sizeof(dateStamp), "%Y%m%d", tmp );

  for( int i = startI; i < startI+num; i++ )
  {
    if( queueDesc[i].pQueue == NULL ) throw Exception( log, log.ERROR, "openStatsFiles: queueDesc[i].pQueue == NULL" );
    if( queueDesc[i].statsFd == -1 )
    {
      queueDesc[i].statsFile = queueDesc[i].name;
      queueDesc[i].statsFile.append( "_" );
      queueDesc[i].statsFile.append( dateStamp );
      queueDesc[i].statsFile.append( ".log" );
      std::string file = queueDesc[i].statsDir;
      file.append( queueDesc[i].statsFile );
      log.info( log.LOGMOSTLY, "openStatsFiles: %s", file.c_str() );
      queueDesc[i].statsFd = open( file.c_str(), O_APPEND|O_WRONLY, S_IRUSR|S_IWUSR|S_IRGRP|S_IWGRP );
      if( (queueDesc[i].statsFd==-1) && (errno==ENOENT) )
      {
        // create the new file and write a header to it
        queueDesc[i].statsFd = open( file.c_str(), O_CREAT|O_APPEND|O_WRONLY, S_IRUSR|S_IWUSR|S_IRGRP|S_IWGRP );
        if( queueDesc[i].statsFd != -1 )
        {
          std::string heading = "time,";
          heading.append( queueDesc[i].pQueue->getStatusKey() );
          heading.append( "\n" );
          int retVal = write( queueDesc[i].statsFd, heading.c_str(), heading.length() );
          if( retVal == -1 ) log.warn( log.LOGMOSTLY, "openStatsFiles: failed to write to file:'%s' fd:%d - %s", file.c_str(), queueDesc[i].statsFd, strerror(errno) );
        } // if
      } // if
      if( queueDesc[i].statsFd == -1 ) log.error( "openStatsFiles: failed to create file:'%s' - %s", file.c_str(), strerror(errno) );
    } // if
    else
      log.error( "openStatsFiles: fd for queue:%s is %d", queueDesc[i].name.c_str(), queueDesc[i].statsFd );
  } // for
} // openStatsFiles

/**
 * creates the stats sub directories
 * @exception on error
 * **/
void nucleus::createStatsDir( int i )
{
  if( queueDesc == NULL ) return;

  // if the stats subdir does not exist try and create it
  umask( S_IWOTH|S_IXOTH );
  struct stat statBuf;
  int retVal = stat( pOptionsNucleus->statsDir.c_str(), &statBuf );
  if( (retVal==-1) || !S_ISDIR(statBuf.st_mode) )
  {
    retVal = mkdir( pOptionsNucleus->statsDir.c_str(), S_IRWXU|S_IRWXG );
    if( retVal == -1 ) throw Exception( log, log.ERROR, "createStatsDir: failed to create directory:'%s' - %s", pOptionsNucleus->statsDir.c_str(), strerror(errno) );
  } // if

  if( queueDesc[i].pQueue == NULL ) throw Exception( log, log.ERROR, "createStatsDir: queueDesc[i].pQueue == NULL" );
  queueDesc[i].statsDir = pOptionsNucleus->statsDir;
  queueDesc[i].statsDir.append( queueDesc[i].name );
  queueDesc[i].statsDir.append( "/" );

  log.info( log.LOGMOSTLY, "createStatsDir: path:'%s'", queueDesc[i].statsDir.c_str() );
  retVal = stat( queueDesc[i].statsDir.c_str(), &statBuf );
  if( (retVal==-1) || !S_ISDIR(statBuf.st_mode) )
  {
    retVal = mkdir( queueDesc[i].statsDir.c_str(), S_IRWXU|S_IRWXG );
    if( retVal == -1 ) throw Exception( log, log.ERROR, "createStatsDir: failed to create directory:'%s' - %s", queueDesc[i].statsDir.c_str(), strerror(errno) );
  } // if
} // createStatsDir

/**
 * drops priviledges from root to the user and the users default group
 * @param user
 * @return true on success
 * **/
bool nucleus::dropPriviledge( const char* user )
{
  if( getuid() != 0 )
  {
    log.warn( log.LOGALWAYS, "dropPriviledge: not running as root, cannot drop priviledge" );
    return true;
  } // if

  struct passwd* pw = getpwnam( user );
  if( pw == NULL ) 
  {
    log.error( "dropPriviledge: could not find user %s", user );
    return false;
  }
  int userId = pw->pw_uid;
  int groupId = pw->pw_gid;
  log.info( log.LOGALWAYS, "dropPriviledge: to user %s uid %d gid %d pid %d ruid:%d, euid:%d", user, userId, groupId, getpid(), getuid(), geteuid() );
  
  int result = setgid( groupId );
  if( result == -1 )
  {
    log.error( "dropPriviledge: failed on setgid - %s", strerror(errno) );
    return false;
  }

  result = utils::dropPriviledgeTemp( userId );
  if( result != 0 )
  {
    log.error( "dropPriviledge: failed on setuid - %s", strerror( errno ) );
    return false;
  }
  log.info( log.LOGALWAYS, "dropPriviledge: after drop ruid:%d, euid:%d", getuid(), geteuid() );
  result = utils::restorePriviledge();
  log.debug( log.LOGALWAYS, "dropPriviledge: after restore ruid:%d, euid:%d", getuid(), geteuid() );
  result = utils::dropPriviledgeTemp( userId );
  log.debug( log.LOGALWAYS, "dropPriviledge: after drop ruid:%d, euid:%d", getuid(), geteuid() );

  // set the process to be dumpable
#ifndef PLATFORM_MAC
  if( prctl( PR_SET_DUMPABLE, 1, 0, 0, 0 ) == -1 )
    log.error( "dropPriviledge: prctl(PR_SET_DUMPABLE) error - %s", strerror(errno) );
#endif

  return true;
} // dropPriviledge

/**
 * sends command to main loop from receiving a signal
 * use a static signal event to prevent signal safe functions in the construction
 * of a new event
 * @param command - the command to send
 * **/
void nucleus::sendSignalCommand( baseEvent::eCommandType command )
{
  int ret = write( signalFd0, (const void*)&command, sizeof(command) );
  if( ret != sizeof(command) ) fprintf( stderr, "nucleus:sendSignalCommand failed to write command %d to fd %d\n", command, signalFd0 );
} // sendSignalCommand

/**
 * signal handler
 * **/
void nucleus::sigHandler( int signo )
{
  switch( signo )
  {
    case SIGTERM:
      bRunning = false;
      sigTermCount++; // this mechanism is a little broken - the main program exits after the first signal .. - send sigterms straight to this process
      break;
    case SIGCHLD:
      sendSignalCommand( baseEvent::CMD_CHILD_SIGNAL );
      break;
    case SIGALRM:
      bTimerTick = true;
      break;
    default:;
      fprintf( stderr, "sigHandler cannot handle signal %s", strsignal( signo ) );
  } // switch
} // sigHandler


