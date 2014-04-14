/**
 contains a collection of queue/worker pairs - in the normal applications there is a single queue for the
 pool of workers and therefor workerPool contains only workers.  for this application there is a queue / 
 worker. has no concept of idle workers
 
 $Id: collectionPool.cpp 2879 2013-06-04 20:05:10Z gerhardus $
 Versioning: a.b.c a is a major release, b represents changes or new features, c represents bug fixes. 
 @version 1.0.0		21/12/2012		Gerhardus Muller		Script created

 @note
 resizeWorkerPool - gaan nie idle workers eers laat gaan nie maar van een kant af delete
 queueContainer::releaseWorker is dependent on the concept of anyAvailableWorkers to feed again
 skryf volgende collectionQueue.*

 @todo
 
 @bug

	Copyright Notice
 * **/

#include "nucleus/collectionPool.h"
#include "nucleus/optionsNucleus.h"
#include "nucleus/recoveryLog.h"
#include "nucleus/baseEvent.h"
#include "nucleus/straightQueue.h"
#include "nucleus/workerDescriptor.h"
#include "nucleus/queueManagementEvent.h"

const char *const collectionPool::FROM = typeid( collectionPool ).name();

/**
 * constructor
 * **/
collectionPool::collectionPool( struct tQueueDescriptor* theQueueDescriptor, recoveryLog* theRecoveryLog, bool bRecovery, int theMainFd )
  : workerPool( theQueueDescriptor, theRecoveryLog, bRecovery, theMainFd, "collectionPool" )
{
} // collectionPool

/**
 * destructor
 * **/
collectionPool::~collectionPool( )
{
} // ~collectionPool

/**
 * retrieves the queue associated with a particular worker
 * **/
baseQueue* collectionPool::getQueueForPid( int pid )
{
  workerDescriptor* pWorker = getWorkerForPid( pid );
  baseQueue* pQueue = pWorker->getQueue();
  if( pQueue == NULL ) throw Exception( log, log.WARN, "getQueueForPid: queue for pid:%d is NULL", pid );
  return pQueue;
} // getQueueForPid

/**
 * retrieves the queue associated with a particular worker
 * **/
baseQueue* collectionPool::getQueueForFd( int fd )
{
  workerDescriptor* pWorker = getWorkerForFd( fd );
  baseQueue* pQueue = pWorker->getQueue();
  if( pQueue == NULL ) throw Exception( log, log.WARN, "getQueueForFd: queue for fd:%d is NULL", fd );
  return pQueue;
} // getQueueForFd

/**
 * iterates through the idleWorkers file descriptors
 * does not remove the worker from the idleWorkers map
 * @param pid - out parameter - corresponding pid or 0
 * @return the fd or -1
 * **/
int collectionPool::getNextIdleFd( int& pid )
{
  if( itIdleWorkers == idleWorkers.end() )
  {
    pid = 0;
log.debug( log.LOGSELDOM, "getNextIdleFd 1 fd:%d pid:%d", -1, pid );
    return -1;
  } // if
  pid = itIdleWorkers->first;
  workerDescriptor* pWorker = itIdleWorkers->second;
  int fd = pWorker->getFd();
  itIdleWorkers++;
log.debug( log.LOGSELDOM, "getNextIdleFd fd:%d pid:%d", fd, pid );
  return fd;
} // getNextIdleFd

/**
 * @return -1 if the worker requested by the event is busy otherwise the worker's fd
 * **/
int collectionPool::anyAvailableWorkers( baseEvent* pEvent )
{
  if( pEvent != NULL )
  {
    int pid = pEvent->getWorkerPid();
    if( pid <= 1 ) throw Exception( log, log.WARN, "anyAvailableWorkers: getWorkerPid returned an invalid value" );  // strictly speaking checking for -1 is sufficient but are crossing language barriers and perhaps we end up with a zero here
    workerDescriptor* pWorker = getWorkerForPid( pid );
    return pWorker->isBusy()?-1:pWorker->getFd();
  } // if
  else
  {
    throw Exception( log, log.WARN, "anyAvailableWorkers: pEvent is NULL" );
  } // else
} // anyAvailableWorkers

/**
 * @param fd - the worker corresponding to the file descriptor;
 * @return 
 * **/
int collectionPool::anyAvailableWorkers( int fd )
{
  // request for a specific fd
  workerDescriptor* pWorker = getWorkerForFd( fd );
  return pWorker->isBusy()?-1:fd;
} // anyAvailableWorkers

/**
 * retrieves and removes the worker from the idleWorkers queue
 * @param pid  -1 for the first in the list - same as idleWorkers.back
 * */
workerDescriptor* collectionPool::getIdleWorkerByPid( int pid )
{
  workerMapIteratorT it;
  if( pid == -1 )
  {
    it = idleWorkers.begin();
    if( it == idleWorkers.end() ) throw Exception( log, log.WARN, "getIdleWorkerByPid: idleWorkers empty pid:%d", pid );
    idleWorkers.erase( it );
    if( it->second == NULL ) throw Exception( log, log.WARN, "getIdleWorkerByPid: worker for pid:%d is NULL", pid );
    log.debug( log.LOGNORMAL, "getIdleWorkerByPid erased pid:%d", pid );
    return it->second;
  } // if
  else
  {
    it = idleWorkers.find( pid );
    if( it == idleWorkers.end() ) throw Exception( log, log.WARN, "getIdleWorkerByPid: workers does not contain pid:%d", pid );
    workerDescriptor* pWorker = it->second;
    idleWorkers.erase( it );
    if( pWorker == NULL ) throw Exception( log, log.WARN, "getIdleWorkerByPid: worker for pid:%d is NULL", pid );
    log.debug( log.LOGNORMAL, "getIdleWorkerByPid 1 erased pid:%d", pid );
    return pWorker;
  } // else
} // getIdleWorkerByPid

/**
 * executes the event
 * accumulate queue stats
 * the event is deleted at the point the worker returns after executing it
 * @param @pEvent
 * @exception on error or failure
 * **/
void collectionPool::executeEvent( baseEvent* pEvent )
{
  if( pEvent == NULL ) throw Exception( log, log.WARN, "executeEvent: pEvent is NULL" );

  // accumulate the queuing stats
  unsigned int timeInQueue = now - pEvent->getQueueTime();
  accQueueTime += timeInQueue;
  if( timeInQueue > maxQueueTime ) maxQueueTime = timeInQueue;
  countQueueEvents++;

  int pid = pEvent->getWorkerPid();
  if( pid <= 1 ) throw Exception( log, log.WARN, "executeEvent: getWorkerPid returned an invalid value" );  // strictly speaking checking for -1 is sufficient but are crossing language barriers and perhaps we end up with a zero here
  workerDescriptor* pWorker = getIdleWorkerByPid( pid );
  pWorker->submitEvent( pEvent, now );
  pWorker->setBusy( true );
  if( log.wouldLog( log.LEVEL6 ) )
    log.info( log.MIDLEVEL ) << "executeEvent: given event to worker " << pWorker->getPid() << ", " << pEvent->toString();
  else
    log.info( log.MIDLEVEL ) << "executeEvent: given event to worker " << pWorker->getPid() << ", '" << pEvent->typeToString() << "'";
} // executeEvent

/**
 * creates a new child/queue
 * @return the pointer to the child or NULL on failure
 * **/
workerDescriptor* collectionPool::createChild( )
{
  workerDescriptor* pWorker = NULL;
  straightQueue* pQueue = NULL;
  try
  {
    pWorker = new workerDescriptor( pContainerDesc, pRecoveryLog, bRecoveryProcess, nucleusFd );
    pQueue = new straightQueue( pContainerDesc, pRecoveryLog, bRecoveryProcess );
    pWorker->setQueue( pQueue );

    // fork the child
    int pid = pWorker->forkChild();
    log.info( log.LOGALWAYS, "createChild: queue '%s' forked child pid %d fd %d %d", queueName.c_str(), pid, pWorker->getFd(), pWorker->getFd1() );
    insertChild( pid, pWorker );
    pQueueManagement->genEvent( queueManagementEvent::QMAN_WSTARTUP, 0, pid );
  } // try
  catch( Exception e )
  {
    if( pWorker != NULL ) delete pWorker;
    if( pQueue != NULL ) delete pQueue;
    pWorker = NULL;
    pQueue = NULL;
  } // catch
  return pWorker;
} // createChild

/**
 * inserts a child int workers, workerFds, idleWorkers
 * **/
void collectionPool::insertChild( int pid, workerDescriptor* pWorker )
{
  workers.insert( std::pair<int,workerDescriptor*>( pid, pWorker ) );
  workerFds.insert( std::pair<int,workerDescriptor*>( pWorker->getFd(), pWorker ) );
  idleWorkers.insert( std::pair<int,workerDescriptor*>( pid, pWorker ) );
  log.debug( log.LOGNORMAL, "insertChild pid:%d fd:%d", pid, pWorker->getFd() );
  baseQueue* pQueue = pWorker->getQueue();
  if( pQueue == NULL ) throw Exception( log, log.WARN, "insertChild: queue for pid:%d is NULL", pid );
  char logName[64];
  sprintf( logName, "queue-%s-%d", queueName.c_str(), pid );
  pQueue->log.setInstanceName( logName );
} // insertChild
