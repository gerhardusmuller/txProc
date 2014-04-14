/** @class workerDescriptor
 workerDescriptor - contains a worker
 
 $Id: workerDescriptor.cpp 2880 2013-06-06 15:39:03Z gerhardus $
 Versioning: a.b.c a is a major release, b represents changes or new features, c represents bug fixes. 
 @version 1.0.0		29/09/2009		Gerhardus Muller		Script created
 @version 1.1.0		30/03/2011		Gerhardus Muller		added a label for createSocketPair and support for non-blocking worker sockets
 @version 1.2.0		23/08/2012		Gerhardus Muller		added a queue member
 @version 1.3.0		30/08/2012		Gerhardus Muller		made provision for a default url, default script and queue management events
 @version 1.4.0		05/06/2013		Gerhardus Muller		support for FD_CLOEXEC

 @note

 @todo
 
 @bug

	Copyright Notice
 */

#include <signal.h>
#include <errno.h>
#include <string.h>
#include <sys/types.h> 
#include <sys/wait.h>
#include <unistd.h>

#include "nucleus/workerDescriptor.h"
#include "nucleus/worker.h"
#include "nucleus/recoveryLog.h"
#include "nucleus/queueManagementEvent.h"
#include "src/options.h"

const char *const workerDescriptor::FROM = typeid( workerDescriptor ).name();
const char *const workerDescriptor::TO_WORKER = typeid( worker ).name();
recoveryLog* workerDescriptor::theRecoveryLog = NULL;
void cleanup( const char* from );  // to be defined by the main application

/**
 Construction
 */
workerDescriptor::workerDescriptor( tQueueDescriptor* theQueueDescriptor, recoveryLog* recovery, bool bRecovery, int theMainFd )
  : object( "workerDescriptor" ),
    pContainerDesc( theQueueDescriptor ),
    nucleusFd( theMainFd )
{
  queueName = pContainerDesc->name;
  persistentApp = pContainerDesc->persistentApp;

  std::string logfile = pOptions->logBaseDir;
  logfile.append( "q_" );
  logfile.append( pContainerDesc->name );
  logfile.append( ".log" );
  log.info( log.LOGALWAYS, "init: queue:%s changing logging to '%s'", pContainerDesc->name.c_str(), logfile.c_str() );
  log.instanceOpenLogfile( logfile.c_str(), pOptionsNucleus->bFlushLogs );
  char tmp[64];
  sprintf( tmp, "workerDesc-%s", queueName.c_str() );
  log.setInstanceName( tmp );

  bRecoveryProcess = bRecovery;
  theRecoveryLog = recovery;
  bChildInShutdown = false;
  pid = 0;
  bBusy = false;
  unixSocket::createSocketPair( fd, tmp );
  char name[64];
  sprintf( name, "workerFd%d", fd[0] );
  pSendSock = new unixSocket( fd[0], unixSocket::ET_WORKER_RET, false, name );
  if( !pContainerDesc->bBlockingWorkerSocket ) pSendSock->setNonblocking();  // it is a problem either way if the socket to the worker blocks unless the packet is very large
  sendFd = fd[0];
  pLastEvent = NULL;
  startTime = 0;
  pQueue = NULL;
  pQueueManagement = new queueManagementEvent( this, pContainerDesc, nucleusFd );
}	// workerDescriptor

/**
 Destruction
 */
workerDescriptor::~workerDescriptor()
{
  dump( "~workerDescriptor " );
  if( pSendSock != NULL ) delete pSendSock;
  if( fd[0] != 0 ) close( fd[0] );
  if( fd[1] != 0 ) close( fd[1] );
  if( pLastEvent != NULL ) delete pLastEvent;
  if( pQueue != NULL ) delete pQueue;
  if( pQueueManagement != NULL ) delete pQueueManagement;
}	// ~workerDescriptor

/**
 * create a child by forking
 * @exception if the pid != 0 or on failure to fork
 * **/
int workerDescriptor::forkChild( )
{
  if( pid != 0 ) throw Exception( log, log.ERROR, "forkChild: pid %d not 0", pid );
  bChildInShutdown = false;
  bSIGTERM = false;
  bBusy = false;
  recoveryReason = "worker_crash";

  if( ( pid = fork( ) ) < 0 )
    throw Exception( log, log.ERROR, "forkChild: failed to fork %s - %s", strerror(errno), toString().c_str(), strerror(errno) );
  else if( pid == 0 )   // child
  {
    try
    {
      // the new and main() has to be in a try / catch otherwise an uncaught
      // exception kills the other children as well
      pid = getpid(); // for logging only
      pWorker = new worker( pContainerDesc, fd[1], theRecoveryLog, bRecoveryProcess, nucleusFd );
      pWorker->main();
      log.generateTimestamp();
      log.info( log.LOGMOSTLY, "forkChild: pWorker->main returned - %s", toString( ).c_str() );
      delete pWorker;
      pWorker = NULL;

      // call system wide cleanup in an attempt to get mem leak testing with Valgrind clean
      // char tmp[32];
      // sprintf( tmp, "workerDescriptor::forkChild pid %d", pid ); 
      // cleanup( tmp );
      exit( 0 );
    } // try
    catch( std::runtime_error e )
    { // json-cpp throws runtime_error
      log.error( "forkChild: caught std::runtime_error:'%s'", e.what() );
      if( pWorker != NULL ) delete pWorker;
      exit( 1 );
    } // catch
    catch( ... )
    {
      log.error( "forkChild: caught exception in forkChild child process" );
      if( pWorker != NULL ) delete pWorker;
      exit( 1 );
    } // catch
  } // if child
  else  // parent
  {
    return pid;
  } // else parent
} // forkChild

/**
 * sends the child a CMD_SHUTDOWN message
 * **/
void workerDescriptor::shutdownChild( )
{
  if( pLastEvent != NULL ) 
  {
    delete pLastEvent;
    pLastEvent = NULL;
  }
  log.info( log.LOGALWAYS, "shutdownChild: pid=%d", pid );
  // update the startTime if not busy so that we do not accidently have a maintenance job kill
  // the worker before it has had a chance to run
  if( !bBusy ) startTime = time( NULL );
  recoveryReason = "CMD_SHUTDOWN";
  bChildInShutdown = true;
  sendCommandToChild( baseEvent::CMD_SHUTDOWN );
} // shutdownChild

/**
 * terminates the child
 * this is typically used to resize the worker pool and it is assumed that
 * the worker is removed from the idle list at the same time.  this correlates
 * with setting bBusy true as the cleanup code would expect it
 * **/
void workerDescriptor::termChild( )
{
  if( pLastEvent != NULL ) 
  {
    delete pLastEvent;
    pLastEvent = NULL;
  }
    
  if( bSIGTERM ) return;
  log.info( log.LOGALWAYS, "termChild: pid=%d", pid );
  bSIGTERM = true;
  recoveryReason = "SIGTERM";
  bBusy = true;
  if( pid != 0 ) 
    kill( pid, SIGTERM );
} // termChild

/**
 * kills (-9) the child
 * **/
void workerDescriptor::killChild( )
{
  log.info( log.LOGALWAYS, "killChild: pid=%d", pid );
  recoveryReason = "SIGKILL";
  bSIGTERM = true;
  if( pid != 0 ) 
    kill( pid, SIGKILL );
} // killChild

/**
 * rips the carpet from under the (typically) persistent process by sending a SIGTERM
 * **/
void workerDescriptor::ripCarpet( )
{
  if( pLastEvent != NULL ) 
  {
    delete pLastEvent;
    pLastEvent = NULL;
  }
    
  log.info( log.LOGALWAYS, "ripCarpet: pid=%d", pid );
  recoveryReason = "RIPPED";
  bBusy = true;
  if( pid != 0 ) 
    kill( pid, SIGTERM );
} // ripCarpet

/**
 * signal child
 * @param sig - the signal to send
 * **/
void workerDescriptor::signalChild( int sig )
{
  if( pid != 0 ) 
    kill( pid, sig );
} // signalChildren

/**
 * send a command event to a child
 * @param command - the command to send
 * **/
void workerDescriptor::sendCommandToChild( baseEvent::eCommandType command )
{
  if( command == baseEvent::CMD_EXIT_WHEN_DONE ) bChildInShutdown = true;

  baseEvent* pEvent = new baseEvent( baseEvent::EV_COMMAND );
  pEvent->setCommand( command );
  pEvent->serialise( sendFd );
  delete pEvent;
} // sendCommandToChild

/**
 * send a command event to a child
 * @param pCommand - the command to send
 * **/
void workerDescriptor::sendCommandToChild( baseEvent* pCommand )
{
  pCommand->serialise( sendFd );
} // sendCommandToChild

/**
 * sends the workers a CMD_EXIT_WHEN_DONE
 * **/
void workerDescriptor::exitWhenDone( )
{
  bChildInShutdown = true;

  // update associated queue if any (collectionQueue
  if( pQueue != NULL ) pQueue->exitWhenDone();
  
  // notify the worker (actually the persistent app) 
  // to commence an orderly shutdown
  baseEvent* pCommand = new baseEvent( baseEvent::EV_COMMAND );
  pCommand->setCommand( baseEvent::CMD_EXIT_WHEN_DONE );
  sendCommandToChild( pCommand );
  delete pCommand;
} // exitWhenDone

/**
 * writes a recovery event if there is a valid previous event
 * **/
void workerDescriptor::writeRecoveryEntry( )
{
  if( pLastEvent != NULL )
  {
    if( !pLastEvent->isRetryExceeded() )
    {
      pLastEvent->incRetryCounter();
      if( theRecoveryLog != NULL )
        theRecoveryLog->writeEntry( pLastEvent, recoveryReason.c_str(), FROM, TO_WORKER );
      else
        log.warn( log.LOGALWAYS ) << "writeRecoveryEntry: (theRecoveryLog is NULL) failed for " << pLastEvent->toString();
    } // if
    else
      log.warn( log.LOGALWAYS )  << "writeRecoveryEntry: retries exceeded dumping event " << pLastEvent->toString();

    delete pLastEvent;
    pLastEvent = NULL;
  } // if
} // writeRecoveryEntry

/**
 * submits an event to the worker for processing
 * **/
void workerDescriptor::submitEvent( baseEvent* pEvent, unsigned int now )
{
  if( pLastEvent != NULL ) delete pLastEvent;   // drop previous backup
  startTime = now;
  bSIGTERM = false;     // this gets set by the maximum execution timeout logic and does not necessarily term the worker - it does however try to terminate the worker's forked task
  recoveryReason = "";
  char trace[64]; snprintf( trace, 64, "tt-%s;", log.getTimestamp() );
  pEvent->appendTrace( trace );
  pEvent->serialise( sendFd );
  pLastEvent = pEvent;                          // keep in case process dies
} // submitEvent

/**
 Standard logging call - produces a generic text version of the workerDescriptor.
 Memory allocation / deleting is handled by this workerDescriptor.
 @return pointer to a string describing the state of the workerDescriptor.  The string has an unspecified lifetime and is not multi-thread safe
 */
std::string workerDescriptor::toString( )
{
  std::ostringstream oss;
  oss << this << " fd:" << fd[0] << "," << fd[1] << " pid:" << pid << (bBusy?" busy":" not busy") << (bChildInShutdown?" in shutdown ":" ") << (bSIGTERM?recoveryReason.c_str():"");
  oss << " startTime:" << startTime;
  if( bBusy && (startTime>0)) oss << " executing for:" << (time(NULL)-startTime) << "s";
	return oss.str();
}	// toString
