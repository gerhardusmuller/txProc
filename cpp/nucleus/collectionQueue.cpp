/**
 the collectionQueue class is a front for the appropriate queue associated with the targeted worker
 
 $Id: collectionQueue.cpp 2879 2013-06-04 20:05:10Z gerhardus $
 Versioning: a.b.c a is a major release, b represents changes or new features, c represents bug fixes. 
 @version 1.0.0		21/12/2012		Gerhardus Muller		Script created

 @note
 submitEvent sequence
   pQueue->canExecuteEventDirectly
   pWorkers->anyAvailableWorkers
   pWorkers->executeEvent
   pQueue->queueEvent
   code bExitWhenDone - paired queues are handled - handle the exit process as seen from nucleus
   code isQueueEmpty

 @todo
 
 @bug

	Copyright Notice
 * **/

#include "nucleus/collectionQueue.h"
#include "nucleus/optionsNucleus.h"
#include "nucleus/recoveryLog.h"
#include "nucleus/baseEvent.h"
#include "nucleus/collectionPool.h"

/**
 * constructor
 * **/
collectionQueue::collectionQueue( tQueueDescriptor* theDescriptor, recoveryLog* theRecoveryLog, bool bRecovery, collectionPool* theWorkers )
  : baseQueue( theDescriptor, theRecoveryLog, bRecovery, "collectionQueue" ),
    pWorkers( theWorkers )
{
  bExitWhenDone = false;
  init();
} // collectionQueue

/**
 * destructor
 * **/
collectionQueue::~collectionQueue( )
{
} // ~collectionQueue

/**
 * init
 * **/
void collectionQueue::init( )
{
  log.setAddPid( true );
  log.info( log.LOGMOSTLY, "init: queue '%s', maxLength %d", queueName.c_str(), maxQueueLength );
} // init

/**
 * **/
bool collectionQueue::canExecuteEventDirectly( baseEvent* pEvent )
{
  int workerPid = pEvent->getWorkerPid();
  if( workerPid <= 1 ) throw Exception( log, log.WARN, "canExecuteEventDirectly workerPid<=1" );
  baseQueue* pQueue = pWorkers->getQueueForPid( workerPid );
  return pQueue->canExecuteEventDirectly( pEvent );
} // canExecuteEventDirectly

/**
 * retrieves the next event from the queue to be executed
 * @return the event or NULL if there are none
 * **/
baseEvent* collectionQueue::popAvailableEvent( int fd )
{
log.debug( log.LOGSELDOM, "popAvailableEvent fd:%d", fd );
  baseQueue* pQueue = pWorkers->getQueueForFd( fd );
  return pQueue->popAvailableEvent( fd );
} // popAvailableEvent

/**
 * queue a new event on the worker's associated queue
 * **/
void collectionQueue::queueEvent( baseEvent* pEvent )
{
  int workerPid = pEvent->getWorkerPid();
  if( workerPid <= 1 ) throw Exception( log, log.WARN, "queueEvent workerPid<=1" );
  baseQueue* pQueue = pWorkers->getQueueForPid( workerPid );

  pQueue->queueEvent( pEvent );
} // queueEvent

/**
 * delegates to the appropriate queues
 * **/
void collectionQueue::scanForExpiredEvents( )
{
  int pid;
  int fd;
  unixSocket* pSocket;
  pWorkers->resetItFd();
  while( (fd=pWorkers->getNextFd(pid,pSocket)) != -1 )
  {
    baseQueue* pQueue =  pWorkers->getQueueForFd( fd );
    pQueue->scanForExpiredEvents();
  } // while
} // scanForExpiredEvents

/**
 * produces a csv version of the queue status and statistics
 * **/
std::string& collectionQueue::getStatus( )
{
  // accumulate the stats
  int pid;
  int fd;
  unixSocket* pSocket;
  pWorkers->resetItFd();
  statusStr.clear();
  while( (fd=pWorkers->getNextFd(pid,pSocket)) != -1 )
  {
    baseQueue* pQueue =  pWorkers->getQueueForFd( fd );
    statusStr.append( pQueue->getStatus() );
    statusStr.append( "," );
    pQueue->resetStats();
  } // while

  return statusStr;
} // getStatus

/**
 * returns the key to getStatus - typically used as a heading to the csv it is stored in
 * **/
std::string& collectionQueue::getStatusKey( )
{
  int pid;
  int fd;
  unixSocket* pSocket;
  pWorkers->resetItFd();
  statusStrKey.clear();
  while( (fd=pWorkers->getNextFd(pid,pSocket)) != -1 )
  {
    baseQueue* pQueue =  pWorkers->getQueueForFd( fd );
    statusStrKey.append( pQueue->getStatusKey() );
    statusStrKey.append( "," );
  } // while
  return statusStrKey;
} // getStatusKey

/**
 * dumps the list to the recovery log
 * expired events are not dumped
 * @param reason
 * **/
void collectionQueue::dumpQueue( const char* reason )
{
  int pid;
  int fd;
  unixSocket* pSocket;
  pWorkers->resetItFd();
  while( (fd=pWorkers->getNextFd(pid,pSocket)) != -1 )
  {
    baseQueue* pQueue =  pWorkers->getQueueForFd( fd );
    pQueue->dumpQueue( reason );
  } // while
} // dumpQueue

