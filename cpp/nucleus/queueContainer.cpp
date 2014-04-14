/**
 Queue container class - owns a workerPool and a baseQueue derived object
 
 $Id: queueContainer.cpp 2879 2013-06-04 20:05:10Z gerhardus $
 Versioning: a.b.c a is a major release, b represents changes or new features, c represents bug fixes. 
 @version 1.0.0		16/09/2009		Gerhardus Muller		Script created
 @version 1.1.0		20/10/2010		Gerhardus Muller		split per queue logging into its own file
 @version 1.2.0		30/03/2011		Gerhardus Muller		added bBlockingWorkerSocket to tQueueDescriptor
 @version 1.2.1		14/08/2012		Gerhardus Muller		pQueue and pWorkers were never deleted in the destructor

 @note

 @todo
 main points to address are:
  submitEvent
 
 @bug

	Copyright Notice
 * **/
#include "nucleus/queueContainer.h"
#include "nucleus/baseEvent.h"
#include "nucleus/recoveryLog.h"
#include "nucleus/straightQueue.h"
#include "nucleus/collectionPool.h"
#include "nucleus/collectionQueue.h"
#include "nucleus/optionsNucleus.h"
#include "nucleus/baseQueue.h"
#include "nucleus/queueManagementEvent.h"
#include "src/options.h"

/**
 * constructor
 * **/
queueContainer::queueContainer( tQueueDescriptor* theQueueDescriptor, recoveryLog* theRecoveryLog, bool bRecovery, int theMainFd, const char* objName )
  : object( objName ),
    pContainerDesc( theQueueDescriptor ),
    pRecoveryLog( theRecoveryLog ),
    bRecoveryProcess( bRecovery ),
    nucleusFd( theMainFd )
{
  pQueue = NULL;
  pWorkers = NULL;
  init();
} // queueContainer

/**
 * destructor
 * **/
queueContainer::~queueContainer()
{
  if( pQueue != NULL ) delete pQueue;
  if( pWorkers != NULL ) delete pWorkers;
  log.info( log.MIDLEVEL, "~queueContainer: '%s' cleaned up", queueName.c_str() );
} // ~queueContainer

/**
 * init
 * **/
void queueContainer::init( )
{
  std::string logfile = pOptions->logBaseDir;
  logfile.append( "q_" );
  logfile.append( pContainerDesc->name );
  logfile.append( ".log" );
  log.info( log.LOGALWAYS, "init: queue:%s changing logging to '%s'", pContainerDesc->name.c_str(), logfile.c_str() );
  log.instanceOpenLogfile( logfile.c_str(), pOptionsNucleus->bFlushLogs );
  log.setAddPid( true );
  char tmp[64];
  sprintf( tmp, "queueContainer-%s", pContainerDesc->name.c_str() );
  log.setInstanceName( tmp );

  // retrieve the queue settings
  std::string key;
  key.assign( pContainerDesc->key ); key.append( "numWorkers" );
  pContainerDesc->numWorkers = pOptionsNucleus->getAsInt( key.c_str(), DEF_NUM_QUEUE_WORKERS );
  key.assign( pContainerDesc->key ); key.append( "maxLength" );
  pContainerDesc->maxLength = pOptionsNucleus->getAsInt( key.c_str(), DEF_MAX_QUEUE_LEN );
  key.assign( pContainerDesc->key ); key.append( "maxExecTime" );
  pContainerDesc->maxExecTime = pOptionsNucleus->getAsInt( key.c_str() );
  key.assign( pContainerDesc->key ); key.append( "persistentApp" );
  pOptionsNucleus->getAsString( key.c_str(), pContainerDesc->persistentApp );
  key.assign( pContainerDesc->key ); key.append( "parseResponseForObject" );
  pContainerDesc->parseResponseForObject = pOptionsNucleus->getAsInt( key.c_str(), 1 );
  key.assign( pContainerDesc->key ); key.append( "bRunPriviledged" );
  pContainerDesc->bRunPriviledged = (bool)pOptionsNucleus->getAsInt( key.c_str(), false );
  key.assign( pContainerDesc->key ); key.append( "bBlockingWorkerSocket" );
  pContainerDesc->bBlockingWorkerSocket = (bool)pOptionsNucleus->getAsInt( key.c_str(), false );
  key.assign( pContainerDesc->key ); key.append( "errorQueue" );
  pOptionsNucleus->getAsString( key.c_str(), pContainerDesc->errorQueue );

  key.assign( pContainerDesc->key ); key.append( "defaultScript" );
  pOptionsNucleus->getAsString( key.c_str(), pContainerDesc->defaultScript );
  key.assign( pContainerDesc->key ); key.append( "defaultUrl" );
  pOptionsNucleus->getAsString( key.c_str(), pContainerDesc->defaultUrl );

  key.assign( pContainerDesc->key ); key.append( "managementQueue" );
  pOptionsNucleus->getAsString( key.c_str(), pContainerDesc->managementQueue );
  key.assign( pContainerDesc->key ); key.append( "managementEventType" );
  std::string val;
  pOptionsNucleus->getAsString( key.c_str(), val );
  if( !val.empty() )
  {
    if( val.compare("EV_PERL")==0 )
      pContainerDesc->managementEventType = baseEvent::EV_PERL;
    else if( val.compare("EV_URL")==0 )
      pContainerDesc->managementEventType = baseEvent::EV_URL;
    else if( val.compare("EV_BIN")==0 )
      pContainerDesc->managementEventType = baseEvent::EV_BIN;
    else if( val.compare("EV_SCRIPT")==0 )
      pContainerDesc->managementEventType = baseEvent::EV_SCRIPT;
    else
      log.warn( log.LOGALWAYS, "init: queue:'%s', managementEventType:'%s' not recognised", pContainerDesc->name.c_str(), val.c_str() );
  } // if

  // parse a comma separated list of values QMAN_WSTARTUP,QMAN_PDIED,QMAN_DONE,QMAN_PSTARTUP
  pContainerDesc->managementEvents = 0;
  key.assign( pContainerDesc->key ); key.append( "managementEvents" );
  pOptionsNucleus->getAsString( key.c_str(), val );
  if( !val.empty() )
  {
    log.debug( log.LOGNORMAL, "init: queue:'%s', eventList:'%s'", pContainerDesc->name.c_str(), val.c_str() );
    std::size_t startPos = 0;
    std::size_t endPos = val.find_first_of( ',', startPos );
    while(!((startPos==std::string::npos)&&(endPos==std::string::npos)))
    {
      std::string sType = val.substr( startPos, endPos-startPos );
      queueManagementEvent::eQManagementType eType = queueManagementEvent::eQManagementStrToType( sType );
      log.debug( log.LOGNORMAL, "init: startPos:%u endPos:%u sCid:%s eType:%d", startPos, endPos, sType.c_str(),eType );
      pContainerDesc->managementEvents |= eType;
      if( endPos != std::string::npos )
      {
        startPos = endPos+1;  // skip the ,
        endPos = val.find_first_of( ',', startPos );
      } // if
      else
        startPos = std::string::npos;
    } // while
    log.debug( log.LOGNORMAL, "init: queue:'%s', eventList:0x%x", pContainerDesc->name.c_str(), pContainerDesc->managementEvents );
  } // if
  else
    pContainerDesc->managementEvents = queueManagementEvent::QMAN_NONE;

  bWorkersFrozen = false;
  bShutdown = false;
  bExitWhenDone = false;
  queueName = pContainerDesc->name;
  queueType = pContainerDesc->type;
  int totalWorkers = pContainerDesc->numWorkers;
  maxQueueLength = pContainerDesc->maxLength;
  maxExecTime = pContainerDesc->maxExecTime;
  persistentApp = pContainerDesc->persistentApp;
  log.info( log.LOGMOSTLY, "init: queue:'%s', type:'%s', numWorkers:%d, maxLength:%d, maxExecTime:%d bRunPriviledged:%d persistentApp:'%s' errorQueue:'%s'",queueName.c_str(),queueType.c_str(),totalWorkers,maxQueueLength,maxExecTime,pContainerDesc->bRunPriviledged,persistentApp.c_str(),pContainerDesc->errorQueue.c_str() );

  if( queueType.compare("straight") == 0 )
  {
    pQueue = new straightQueue( pContainerDesc, pRecoveryLog, bRecoveryProcess );
    pWorkers = new workerPool( pContainerDesc, pRecoveryLog, bRecoveryProcess, nucleusFd );
  } // if
  else if( queueType.compare("collection") == 0 )     // single queue per worker
  {
    collectionPool* theWorkers = new collectionPool( pContainerDesc, pRecoveryLog, bRecoveryProcess, nucleusFd );   // contains a collection of queue/worker pairs
    pQueue = new collectionQueue( pContainerDesc, pRecoveryLog, bRecoveryProcess, theWorkers );     // this becomes literally a front for the appropriate queue associated with the targeted worker
    pWorkers = theWorkers;
  } // else if
  else
    throw new Exception( log, log.ERROR, "init: queue type:'%s' not supported", queueType.c_str() );
} // init

/**
 * reopens the instance log files
 * **/
void queueContainer::reopenLogfile( )
{
  log.instanceReopenLogfile();
  pWorkers->reopenLogfile();
  pQueue->reopenLogfile();
} // reopenLogfile

/**
 * submits an event to the queue for eventual processing
 * the event is either deleted after executing or queued
 * @param pEvent
 * @exception on error
 * **/
void queueContainer::submitEvent( baseEvent* pEvent )
{
  if( pEvent == NULL ) throw new Exception( log, log.WARN, "submitEvent: pEvent is NULL" );
  
  // set the queue time
  // calculate expiry time if requested
  pEvent->setQueueTime( now );
  int lifetime = pEvent->getLifetime( );
  if( lifetime != -1 ) pEvent->setExpiryTime( lifetime + now );

  // execute directly if we can otherwise queue
  if( !bWorkersFrozen )
  {
    bool bOkToExecuteImmediately = pQueue->canExecuteEventDirectly( pEvent );
    bool bWorkerAvailable = (pWorkers->anyAvailableWorkers(pEvent)!=-1);            // if relevant find the worker targeted by this event
log.debug( log.LOGONOCCASION, "submitEvent: bOkToExecuteImmediately:%d bWorkerAvailable:%d", bOkToExecuteImmediately, bWorkerAvailable );
    if( bOkToExecuteImmediately && bWorkerAvailable )
    {
      pWorkers->executeEvent( pEvent );
      return;
    } // if
  } // if

  // queue it
  pQueue->queueEvent( pEvent );
} // submitEvent

/**
 * feeds any available workers if there are any events queued
 * normally only required if the worker pool size has been adjusted upwards
 * **/
void queueContainer::feedWorker( )
{
  // this only has meaning for the normal queuing / workerpool concept
  // by the time we have done this amount of work we might as well iterate through all
  if( !pWorkers->anyAvailableWorkers() ) return;
  if( pQueue->isQueueEmpty() ) return;

  // we would like to iterate while there are workers and events to feed them. for a normal workerpool
  // the termination condition is when we run out of either idle workers or the queue empty
  // for a collection pool the termination condition is when we run out of idle workers
  int fd = -1;
  int pid = 0;
  pWorkers->resetIdleIt();
  while( ((fd=pWorkers->getNextIdleFd(pid))!=-1) && !pQueue->isQueueEmpty() )
  {
log.debug( log.LOGSELDOM, "feedWorker fd:%d", fd );
    baseEvent* pEvent = pQueue->popAvailableEvent( fd );
    if( pEvent != NULL ) pWorkers->executeEvent( pEvent );
  } // while
} // feedWorker

/**
 * releases the worker for the associated event
 * immediately feeds the worker again
 * @param fd
 * @param pEvent
 * **/
void queueContainer::releaseWorker( int fd, baseEvent* pEvent )
{
  pWorkers->releaseWorker( fd, pEvent );
  
  // there should normally be an available worker now - for the collection class we want the same worker back again
  if( !bWorkersFrozen )
  {
    if( pWorkers->anyAvailableWorkers( fd ) )
    {
log.debug( log.LOGNORMAL, "releaseWorker fd:%d", fd );
      baseEvent* pNew = pQueue->popAvailableEvent( fd );
      if( pNew != NULL ) pWorkers->executeEvent( pNew );
    } // if
    else
      log.warn( log.LOGALWAYS, "releaseWorker: no available workers" );
  } // if
  else
    log.warn( log.LOGALWAYS, "releaseWorker: queue frozen" );
} // releaseWorker

/**
 * sends the workers a CMD_EXIT_WHEN_DONE and feed CMD_END_OF_QUEUE
 * as soon as the queues are empty
 * **/
void queueContainer::exitWhenDone( )
{
  // as soon as the  queue is empty all workers will be fed CMD_END_OF_QUEUE events
  // persistent apps should immediatly exit on receiving these
  bExitWhenDone = true;
  pQueue->exitWhenDone();
  
  // notify the workers (actually the persistent app) that they should
  // commence an orderly shutdown
  pWorkers->exitWhenDone();
  feedWorker();
} // exitWhenDone

/**
 * processes a reconfigure command CMD_WORKER_CONF
 * @param pCommand
 * **/
void queueContainer::reconfigureCmd( baseEvent* pCommand )
{
  if( pCommand->getCommand() == baseEvent::CMD_WORKER_CONF )
    pWorkers->reconfigure( pCommand );
  else
    log.warn( log.LOGALWAYS, "reconfigureCmd: unable to process command:%s", pCommand->commandToString() );
} // reconfigureCmd

/**
 * freezes and unfreezes queue execution
 * @param bFreeze
 * **/
void queueContainer::freeze( bool bFreeze )
{
  log.info( log.LOGALWAYS, "freeze:%d", bFreeze );
  if( bFreeze )
    bWorkersFrozen = true;
  else
  {
    bWorkersFrozen = false;
    for( int i = 0; i < pWorkers->getTotalWorkers(); i ++ )
      feedWorker();
  } // else
} // freeze

/**
 * **/
void queueContainer::dumpHttp( baseEvent* pEvent )
{
} // dumpHttp

/**
 * getStatus implementation in straightQueue and workerPool resets the stats
 * **/
void queueContainer::resetStats( )
{
  pQueue->resetStats();
  pWorkers->resetStats();
} // resetStats

/**
 * **/
void queueContainer::maintenance( )
{
} // maintenance

/**
 * produces a csv version of the queue status and statistics
 * **/
std::string& queueContainer::getStatus( bool bLog )
{
  char stat[128];
  sprintf( stat, "%d,%d,", bWorkersFrozen, bShutdown );
  statusStr = stat;
  statusStr.append( pQueue->getStatus() );
  statusStr.append( "," );
  statusStr.append( pWorkers->getStatus() );
  if( statusStrKey.empty() ) getStatusKey();

  if( bLog ) log.info( log.LOGNORMAL, "getStatus: queue:'%s'(%s):%s (%s)", queueName.c_str(), queueType.c_str(), statusStr.c_str(), statusStrKey.c_str() );
  return statusStr;
} // getStatus

/**
 * returns the key to getStatus - typically used as a heading to the csv it is stored in
 * **/
std::string& queueContainer::getStatusKey( )
{
  statusStrKey = "frozen,shutdown,";
  statusStrKey.append( pQueue->getStatusKey() );
  statusStrKey.append( "," );
  statusStrKey.append( pWorkers->getStatusKey() );

  return statusStrKey;
} // getStatusKey
