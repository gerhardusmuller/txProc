/**
 worker pool class
 
 $Id: workerPool.cpp 2629 2012-10-19 16:52:17Z gerhardus $
 Versioning: a.b.c a is a major release, b represents changes or new features, c represents bug fixes. 
 @version 1.0.0		10/09/2008		Gerhardus Muller		Script created
 @version 1.1.0		20/10/2010		Gerhardus Muller		split per queue logging into its own file
 @version 1.2.0		14/08/2012		Gerhardus Muller		better cleanup in case the children did not exit by the time the object is destroyed
 @version 2.0.0		16/08/2012		Gerhardus Muller		support for individually addressable workers; propagation of dynamic execTimeLimit to the actual worker
 @version 2.1.0		03/09/2012		Gerhardus Muller		queue management events
 @version 2.2.0		04/09/2012		Gerhardus Muller		getNextFd to return associated unixSocket as well

 @note
 vir addressable workers:
  executeEvent moet waarskynlik overloaded word - dit check available workers en pop 'n werker af van idleWorkers af
  createChild - push werkers in idleWorkers in
  removeChild - haal die werker uit idleWorkers uit
  respawnChild - daar is 'n interested party wat wil weet as 'n child ge-exit het
  isIdle - die konsep moet anders wees vir addressable werkers
  countIdle - moet waarskynlik altyd in long mode loop
  releaseWorker - gebruik die konsep van idleWorkers

 @todo
 
 @bug

	Copyright Notice
 * **/

#include "nucleus/workerPool.h"
#include "nucleus/baseEvent.h"
#include "nucleus/workerDescriptor.h"
#include "nucleus/worker.h"
#include "nucleus/optionsNucleus.h"
#include "nucleus/recoveryLog.h"
#include "nucleus/queueContainer.h"
#include "nucleus/queueManagementEvent.h"
#include "src/options.h"

/**
 * constructor
 * **/
workerPool::workerPool( struct tQueueDescriptor* theQueueDescriptor, recoveryLog* theRecoveryLog, bool bRecovery, int theMainFd, const char* name )
  : object( name ),
    pContainerDesc( theQueueDescriptor ),
    pRecoveryLog( theRecoveryLog ),
    bRecoveryProcess( bRecovery ),
    nucleusFd( theMainFd )
{
  queueName = pContainerDesc->name;
  totalWorkers = pContainerDesc->numWorkers;
  execTimeLimit = pContainerDesc->maxExecTime;
  bExitWhenDone = false;
  if( pContainerDesc->persistentApp.empty() )
    bPersistentApp = false;
  else
    bPersistentApp = true;

  pQueueManagement = new queueManagementEvent( pContainerDesc, nucleusFd );
  init();
} // workerPool

/**
 * destructor
 * **/
workerPool::~workerPool( )
{
  // assumption is proper cleanup and respawnChild is called for child 
  // that has exited which deletes its workerDescriptor object
  // in case we are forced to exit and and the children are not dead we clean
  // up best case and run away
  workerMapIteratorT it;
  for( it = workers.begin(); it != workers.end(); it++ )
  {
    int pid = it->first;
    respawnChild( pid, false );
  } // for
  if( pQueueManagement != NULL ) delete pQueueManagement;
} // ~workerPool

/**
 * init - creates the workers
 * **/
void workerPool::init( )
{
  std::string logfile = pOptions->logBaseDir;
  logfile.append( "q_" );
  logfile.append( pContainerDesc->name );
  logfile.append( ".log" );
  log.info( log.LOGALWAYS, "init: queue:%s changing logging to '%s'", pContainerDesc->name.c_str(), logfile.c_str() );
  log.instanceOpenLogfile( logfile.c_str(), pOptionsNucleus->bFlushLogs );
  char tmp[64];
  sprintf( tmp, "%s-%s", log.getInstanceName().c_str(),queueName.c_str() );
  log.setInstanceName( tmp );
  log.setAddPid( true );
  resetStats();

  // create the workers
  for( int i = 0; i < totalWorkers; i++ )
    createChild();

  log.info( log.LOGMOSTLY, "init: totalWorkers:%d execTimeLimit:%d", totalWorkers, execTimeLimit );
} // init

/**
 * reopens the instance log files
 * **/
void workerPool::reopenLogfile( )
{
  log.instanceReopenLogfile();
  workerMapIteratorT it;
  for( it = workers.begin(); it != workers.end(); it++ )
  {
    workerDescriptor* pWorker = it->second;
    pWorker->reopenLogfile();
  } // for
} // reopenLogfile

/**
 * resets statistics
 * **/
void workerPool::resetStats( )
{
  numRecoveryEvents = 0;
  accQueueTime = 0;
  maxQueueTime = 0;
  countQueueEvents = 0;
  accExecTime = 0;
  maxExecTime = 0;
  countExecEvents = 0;
} // resetStats

/**
 * executes the event
 * accumulate queue stats
 * the event is deleted at the point the worker returns after executing it
 * @param @pEvent
 * @exception on error or failure
 * **/
void workerPool::executeEvent( baseEvent* pEvent )
{
  if( pEvent == NULL ) throw new Exception( log, log.WARN, "executeEvent: pEvent is NULL" );
  if( !anyAvailableWorkers() )  throw new Exception( log, log.WARN, "executeEvent: no available workers" );

  // accumulate the queuing stats
  unsigned int timeInQueue = now - pEvent->getQueueTime();
  accQueueTime += timeInQueue;
  if( timeInQueue > maxQueueTime ) maxQueueTime = timeInQueue;
  countQueueEvents++;

  workerDescriptor* pWorker = getIdleWorkerByPid( -1 );
  pWorker->submitEvent( pEvent, now );
  pWorker->setBusy( true );
  if( log.wouldLog( log.LEVEL6 ) )
    log.info( log.MIDLEVEL ) << "executeEvent: given event to worker " << pWorker->getPid() << ", " << pEvent->toString();
  else
    log.info( log.MIDLEVEL ) << "executeEvent: given event to worker " << pWorker->getPid() << ", '" << pEvent->typeToString() << "'";
} // executeEvent

/**
 * updates the max execution time
 * **/
void workerPool::setMaxExecTime( unsigned int m )
{
  execTimeLimit = m;
  pContainerDesc->maxExecTime = execTimeLimit;
  baseEvent cmd( baseEvent::EV_COMMAND );
  cmd.setCommand( baseEvent::CMD_WORKER_CONF );
  cmd.addParam( "cmd", "updatemaxexectime" );
  cmd.addParam( "val", m );
  sendCommandToChildren( &cmd );
  log.info( log.LOGALWAYS, "setMaxExecTime: queue '%s' max execution time %us", queueName.c_str(), execTimeLimit );
} // setMaxExecTime

/**
 * creates a new child
 * @return the pointer to the child or NULL on failure
 * **/
workerDescriptor* workerPool::createChild( )
{
  workerDescriptor* pWorker = NULL;
  try
  {
    pWorker = new workerDescriptor( pContainerDesc, pRecoveryLog, bRecoveryProcess, nucleusFd );

    // fork the child
    int pid = pWorker->forkChild( );
    log.info( log.LOGALWAYS, "createChild: queue '%s' forked child pid %d fd %d %d", queueName.c_str(), pid, pWorker->getFd(), pWorker->getFd1() );
    insertChild( pid, pWorker );
    pQueueManagement->genEvent( queueManagementEvent::QMAN_WSTARTUP, 0, pid );
  } // try
  catch( Exception e )
  {
  } // catch
  return pWorker;
} // createChild

/**
 * alters the number of workers
 * @return number of new workers (+) for more or (-) for workers deleted or to be deleted
 * **/
int workerPool::resizeWorkerPool( int newNumWorkers )
{
  int deltaNumWorkers = newNumWorkers - totalWorkers;
  log.info( log.LOGALWAYS, "resizeWorkerPool: queue '%s' from %d workers to %d delta %d", queueName.c_str(), totalWorkers, newNumWorkers, deltaNumWorkers );
  if( deltaNumWorkers == 0 ) return deltaNumWorkers;

  if( newNumWorkers > totalWorkers )
  {
    // easier one of the 2 - simply create more workers and add them
    for( int i = totalWorkers; i < newNumWorkers; i++ )
      createChild( );
    totalWorkers = newNumWorkers;
  } // if
  else
  {
    // more difficult - if the workers are idle kill them else wait for 
    // them to finish before killing them
    // start off by killing any idle workers - the balance will have to come 
    // from workers busy at the moment
    int numWorkersToKill = totalWorkers - newNumWorkers;
    int numIdleToKill = ((unsigned int)numWorkersToKill>idleWorkersSize())?idleWorkersSize():numWorkersToKill;
    int numBusyToKill = numWorkersToKill - numIdleToKill;
    log.info( log.LOGALWAYS, "resizeWorkerPool: queue '%s' total of %d to kill, %d from idle pool, %d that are currently busy", queueName.c_str(), numWorkersToKill, numIdleToKill, numBusyToKill );
    for( int i = 0; i < numIdleToKill; i++ )
    {
      workerDescriptor* pWorker = getIdleWorkerByPid( -1 );
      pWorker->shutdownChild();
      pWorker->setBusy( true );
    } // for

    // fill our quota from workers busy at the moment
    if( numBusyToKill > 0 )
    {
      workerMapIteratorT it;
      for( it = workers.begin(); (numBusyToKill>0)&&(it != workers.end()); it++ )
      {
        workerDescriptor* pWorker = it->second;
        if( !pWorker->isTerminal() )
        {
          pWorker->shutdownChild();
          numBusyToKill--;
        } // if
      } // for
      if( numBusyToKill != 0 ) log.warn( log.LOGALWAYS, "resizeWorkerPool: numBusyToKill is %d at end", numBusyToKill );
    } // if
  } // else

  return deltaNumWorkers;
} // resizeWorkerPool

/**
 * reads the pid of the next worker on the idleWorker deque - 
 * same one as getIdleWorkerByPid would pop
 * does not remove the worker from the idleWorkers map
 * @param pid - out parameter - corresponding pid or 0
 * @return the fd or -1
 * **/
int workerPool::getNextIdleFd( int& pid )
{
  if( idleWorkers.empty() )
  {
    pid = 0;
    return -1;
  } // if
  pid=idleWorkers.back();   // no pop_back!
  workerDescriptor* pWorker = workers[pid];
  int fd = pWorker->getFd();
  return fd;
} // getNextIdleFd

/**
 * iterates through the file descriptors
 * @param pid - out parameter - corresponding pid or 0
 * @return the fd or -1
 * **/
int workerPool::getNextFd( int& pid, unixSocket*& pSocket )
{
  if( itFd == workerFds.end() )
  {
    pid = 0;
    return -1;
  } // if
  int fd = itFd->first;
  workerDescriptor* pWorker = itFd->second;
  pid = pWorker->getPid();
  pSocket = pWorker->getSock();
  itFd++;
  return fd;
} // getNextFd

/**
 * returns the associated worker unixSocket object
 * **/
unixSocket* workerPool::getWorkerSock( int fd )
{
  workerMapIteratorT it;
  it = workerFds.find( fd );
  if( it != workerFds.end() )
  {
    workerDescriptor* pWorker = it->second;
    return pWorker->getSock();
  } // if
  else
    return NULL;
} // getWorkerSock

/**
 * terminates all children - cannot wait for the children to exit as there can
 * be multiple queues - dispatcher has to do the waiting
 * should only be called from the destructor
 * **/
void workerPool::termChildren( )
{
  // cleanup the worker pool by signalling every child
  log.debug( log.MIDLEVEL, "termChildren queue:'%s' entered", queueName.c_str() );
  workerMapIteratorT it;
  for( it = workers.begin(); it != workers.end(); it++ )
  {
    workerDescriptor* pWorker = it->second;
    if( pWorker->isKilled() )
        pWorker->killChild();     // last resort - kills the worker without options
    else if( pWorker->isTerminal() )
        pWorker->termChild();     // this should terminate a stuck persistent app but does not kill or terminate the worker without its cooperation
    else
      pWorker->shutdownChild( );  // orderly cooperative shutdown
  } // for
} // termChildren

/**
 * deletes a specific entry in idleWorkers
 * **/
void workerPool::deleteIdleWorkersEntry( int pid )
{
  idleWorkersIteratorT itW = idleWorkers.begin();
  bool bFound = false;
  while( !bFound && (itW != idleWorkers.end()) )
  {
    if( *itW == pid ) 
    {
      log.debug( log.LOGMOSTLY, "deleteIdleWorkersEntry: pid:%d from idle queue", pid );
      idleWorkers.erase( itW );
      bFound = true;
    }
    itW++;
  } // while
} // deleteIdleWorkersEntry

/**
 * removes a child from workers, workerFds and the idleWorkers 
 * @return the workerDescriptor object or NULL if not found
 * **/
workerDescriptor* workerPool::removeChild( int pid )
{
  workerDescriptor* pWorker = NULL;
  
  workerMapIteratorT it;
  it = workers.find( pid );
  if( it != workers.end( ) )
  {
    pWorker = it->second;
    workers.erase( it );

    // remove the entry from workerFds
    int fd = pWorker->getFd( );
    workerMapIteratorT it1;
    it1 = workerFds.find( fd );
    if( it1 != workerFds.end( ) )
    {
      log.debug( log.LOGMOSTLY, "removeChild: pid:%d from workers queue", pid );
      workerFds.erase( it1 );
    } // if
    else
      log.error( "removeChild: fd:%d for pid %d not in workerFds", queueName.c_str(), fd, pid );

    // if idle also remove from the idleWorkers
    if( !pWorker->isBusy() ) deleteIdleWorkersEntry( pid );
  } // if
  else
    log.warn( log.LOGALWAYS, "removeChild: queue:%s pid:%d not in workers", queueName.c_str(), pid );
  
  return pWorker;
} // removeChild

/**
 * inserts a child into workers, workerFds, idleWorkers
 * **/
void workerPool::insertChild( int pid, workerDescriptor* pWorker )
{
  workers.insert( std::pair<int,workerDescriptor*>( pid, pWorker ) );
  workerFds.insert( std::pair<int,workerDescriptor*>( pWorker->getFd(), pWorker ) );
  addIdleWorkersEntry( pid, pWorker );
} // insertChild

/**
 * retrieves the worker for a particular pid - workerPool should be retrofitted to use this
 * **/
workerDescriptor* workerPool::getWorkerForPid( int pid )
{
  workerMapIteratorT it;
  it = workers.find( pid );
  if( it == workers.end() ) throw Exception( log, log.WARN, "getWorkerForPid: workers does not contain pid:%d", pid );
  workerDescriptor* pWorker = it->second;
  if( pWorker == NULL ) throw Exception( log, log.WARN, "getWorkerForPid: worker for pid:%d is NULL", pid );
  return pWorker;
} // getWorkerForPid

/**
 * retrieves the worker for a particular fd - workerPool should be retrofitted to use this
 * **/
workerDescriptor* workerPool::getWorkerForFd( int fd )
{
  workerMapIteratorT it;
  it = workerFds.find( fd );
  if( it == workerFds.end() ) throw Exception( log, log.WARN, "getWorkerForFd: workers does not contain fd:%d", fd );
  workerDescriptor* pWorker = it->second;
  if( pWorker == NULL ) throw Exception( log, log.WARN, "getWorkerForFd: worker for fd:%d is NULL", fd );
  return pWorker;
} // getWorkerForFd

/**
 * respawns a child that exited
 * @param childPid - the pid of the child that exited
 * @param bRespawn - true to respawn otherwise cleanup
 * @return the new child pid if the child was owned by this queue and restarted, -1 if terminal and 0 otherwise
 * **/
int workerPool::respawnChild( int childPid, bool bRespawn )
{
  int newPid = 0;

  log.info( log.LOGMOSTLY, "respawnChild: childPid:%d bRespawn:%d bPersistentApp:%d", childPid, bRespawn, bPersistentApp );
  workerDescriptor* pWorker = removeChild( childPid );
  if( pWorker != NULL )
  {
    if( pWorker->isTerminal() ) newPid = -1;
    if( bRespawn && !pWorker->isTerminal() )
    {
      try
      {
        pWorker->writeRecoveryEntry( );   // write a recovery entry for the crashed worker
        numRecoveryEvents++;
        pWorker->setPid( 0 );
        newPid = pWorker->forkChild( );
        insertChild( newPid, pWorker );
        pQueueManagement->genEvent( queueManagementEvent::QMAN_WSTARTUP, childPid, newPid );
      } // try
      catch( Exception e )
      {
      } // catch
    } // if( bRespawn
    else
    {
      pQueueManagement->genEvent( queueManagementEvent::QMAN_WSTARTUP, childPid, 0 );
      delete pWorker;
      totalWorkers--;
    } // else
  } // if
  return newPid;
} // respawnChild

/**
 * signal all children
 * @param sig - the signal to send
 * **/
void workerPool::signalChildren( int sig )
{
  workerMapIteratorT it;
  for( it = workers.begin(); it != workers.end(); it++ )
  {
    workerDescriptor* pWorker = it->second;
    pWorker->signalChild( sig );
  } // for
} // signalChildren

/**
 * sends command requests to the children
 * @param pCommand - the command to send
 * **/
void workerPool::sendCommandToChildren( baseEvent* pCommand )
{
  workerMapIteratorT it;
  for( it = workers.begin(); it != workers.end(); it++ )
  {
    workerDescriptor* pWorker = it->second;
    pWorker->sendCommandToChild( pCommand );
  } // for
} // sendCommandToChildren

/**
 * sends the workers a CMD_EXIT_WHEN_DONE
 * **/
void workerPool::exitWhenDone( )
{
  // as soon as the  queue is empty all workers will be fed CMD_END_OF_QUEUE events
  // persistent apps should immediatly exit on receiving these
  bExitWhenDone = true;
  
  workerMapIteratorT it;
  for( it = workers.begin(); it != workers.end(); it++ )
  {
    workerDescriptor* pWorker = it->second;
    pWorker->exitWhenDone();
  } // for
} // exitWhenDone

/**
 * this function behaves differently when we run a persistent app. persistent apps
 * are given the opportunity to shutdown and cleanup at the appropriate time and
 * are sent a CMD_EXIT_WHEN_DONE by nucleus to encourage them.  nucleus' criteria
 * of when to exit is that the queues should be idle. for a persistent app this is when
 * it has exited. it is up to the persistent app to empty its queue before it chooses to 
 * exit
 * **/
bool workerPool::isIdle( )
{
  if( bExitWhenDone )
  {
    if( totalWorkers==0 )
      return true;
    else
    {
      log.info( log.LOGALWAYS, "isIdle: %d workers still alive", totalWorkers );
      return false;
    } // else
  } // if
  else
    return countIdle(true)==totalWorkers; // very conservative, slow and only for debugging
    // return countIdle(false)==totalWorkers; - speed does not matter as it is only invoked on shutdown
} // isIdle

/**
 * sends a reconfigure command to all workers
 * may later on want to do filter or do additional processing
 * @param pCommand
 * **/
void workerPool::reconfigure( baseEvent* pCommand )
{
  sendCommandToChildren( pCommand );
} // reconfigure

/**
 * counts the idle workers
 * @param bCountLong - true to iterate and count rather than take the length of the idle queue only
 * @return idleCount
 * **/
int workerPool::countIdle( bool bCountLong )
{
  if( bCountLong )
  {
    unsigned int idleCount = 0;
    workerMapIteratorT it;
    for( it = workers.begin(); it != workers.end(); it++ )
    {
      workerDescriptor* pWorker = it->second;
      if( !pWorker->isBusy( ) ) idleCount++;
    } // for
    if( idleCount != idleWorkersSize() )
      log.warn( log.LOGALWAYS, "countIdle: queue %s - discrepancy idleCount %d idle queue len %d", queueName.c_str(), idleCount, idleWorkersSize() );
    return idleCount;
  }
  else
    return idleWorkersSize();
} // countIdle

/**
 * releases a worker
 * service a return from one of the children
 * normally we only put it back onto the idle queue
 * only ever expect a single event back per child
 * if a worker has been terminated do not re-insert it into the idle queue
 * **/
void workerPool::releaseWorker( int fd, baseEvent* pEvent )
{
  workerMapIteratorT it;
  it = workerFds.find( fd );
  if( it != workerFds.end() )
  {
    workerDescriptor* pWorker = it->second;
    if( pEvent != NULL ) 
    {
      if( pEvent->getType() == baseEvent::EV_WORKER_DONE )
      {
        // if the worker is not busy assume it was a persistent process and killed
        // to reload
        if( pWorker->isBusy() && !pWorker->isTerminal() )
        {
          pWorker->setBusy( false );
          addIdleWorkersEntry( pWorker->getPid(), pWorker );
        } // if
        updateStats( pEvent );

        log.debug( log.MIDLEVEL, "releaseWorker: worker:%d fd:%d finished isTerminal:%d", pWorker->getPid(), fd, pWorker->isTerminal() );
      } // if EV_WORKER_DONE
      else
        log.warn( log.LOGALWAYS ) << "releaseWorker does not know how to handle return event " << pEvent->toString() << " from worker " << pWorker->getPid() << " fd:" << fd;
    } // if( pEvent
    else
      log.warn( log.LOGALWAYS ) << "releaseWorker failed to unserialise worker return - " << pWorker->toString();
  } // if( it != workerFds.end
  else
    log.error( "releaseWorker fd %d is not in workerFds", fd );
} // releaseWorker

/**
 * updates the stats of the time take for process / url execution
 * @param pDone - the return event from the child
 * **/
void workerPool::updateStats( baseEvent* pDone )
{
  unsigned int elapsedTime = pDone->getElapsedTime();
  accExecTime += elapsedTime;
  if( elapsedTime > maxExecTime ) maxExecTime = elapsedTime;
  countExecEvents++;

  if( pDone->getRecoveryEvent() ) 
    numRecoveryEvents++;
} // updateStats

/**
 * finds and kills any worker overrunning its execution time
 * **/
void workerPool::checkOverrunningWorkers( )
{
  if(execTimeLimit  == 0 ) return;

  workerMapIteratorT it;
  for( it = workers.begin(); it != workers.end(); it++ )
  {
    workerDescriptor* pWorker = it->second;
    if( pWorker->isBusy() && ((now-pWorker->getStartTime())>execTimeLimit) )
    {
      log.warn( log.LOGALWAYS, "checkOverrunningWorkers: killing %s", pWorker->toString().c_str() );
      // first time round send the child a SIGTERM, second time round a SIGKILL
      if( pWorker->isKilled() )
        pWorker->killChild();
      else
        pWorker->termChild();
    } // if
    else
    {
      if( log.wouldLog( log.LOGONOCCASION ) && pWorker->isBusy() )
        log.info( log.LOGALWAYS, "checkOverrunningWorkers: worker OK::%s execTimeLimit:%d", pWorker->toString().c_str(), execTimeLimit );
    } // else
  } // 
} // checkOverrunningWorkers

/**
 * produces a csv version of the queue status and statistics
 * **/
std::string& workerPool::getStatus( )
{
  char str[128];
  float meanExecTime = (countExecEvents>0)?(float)accExecTime/countExecEvents:0;
  float meanQueue = (countQueueEvents>0)?(float)accQueueTime/countQueueEvents:0;
  sprintf( str, "%u,%u,%u,%f,%u,%u,%f,%d,%u",execTimeLimit,countExecEvents,maxExecTime,meanExecTime,countQueueEvents,maxQueueTime,meanQueue,totalWorkers,(unsigned int)idleWorkersSize() );

  resetStats();
  statusStr = str;
  return statusStr;
} // getStatus

/**
 * returns the key to getStatus - typically used as a heading to the csv it is stored in
 * **/
std::string& workerPool::getStatusKey( )
{
  statusStrKey = "timeLimit,cntExec,mxExec,mnExec,cntQ,mxQ,mnQ,cntW,idleW";
  return statusStrKey;
} // getStatusKey
