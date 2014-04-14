/** @class batchQueue
 Event queue class supporting round robin scheduling across batches of events
 Events are submitted as queueName:key. If the key is omitted it is assumed 0 and put into the main queue.  
 If the key is present a batch with that key is looked for and added to it.
 Batches will be round robin scheduled with a configurable bias towards the main queue
 only integer keys are supported

 The scheme is as follows.  Events can be scheduled onto the queue with some 
 identifier following the queue name separated by a colon.  If such an identifier
 is not given the queue behaves exactly the same as the normal baseQueue.  
 If an event is queued with a sub queue id a sub queue is created with this id
 if it does not already exist and it is placed on this queue.  feedWorker behaves
 differently when there are additional queues. The workers now feed from all the 
 available queues in round-robin fashion with a potential bias towards the main queue
 ie the main queue may be sampled 3 times for every once the other queues are. this 
 bias should most likely be quite heavy.
 
 $Id: batchQueue.cpp 31 2009-09-22 12:27:18Z gerhardus $
 Versioning: a.b.c a is a major release, b represents changes or new features, c represents bug fixes. 
 @version 1.0.0		10/09/2009		Gerhardus Muller		Script created

 @note

 @todo
 make sure queue0 is seen to be the main queue
 
 @bug

	Copyright Notice
 */

#include "nucleus/batchQueue.h"
#include "nucleus/optionsNucleus.h"
#include "nucleus/baseEvent.h"
#include "nucleus/recoveryLog.h"

const char *const batchQueue::FROM = typeid( batchQueue ).name();

/**
 * constructor
 * **/
batchQueue::batchQueue( tQueueDescriptor* theDescriptor, recoveryLog* theRecoveryLog, bool bRecovery )
  : baseQueue( theDescriptor, theRecoveryLog, bRecovery, "batchQueue" )
{
  init();
} // batchQueue

/**
 * destructor
 * **/
batchQueue::~batchQueue()
{
  log.info( log.MIDLEVEL, "~batchQueue: '%s' %d remaining events", queueName.c_str(), delayList.size() );
} // ~batchQueue

/**
 * init 
 * **/
void batchQueue::init( )
{
  std::string key( optionsKey ); key.append( "numSubQueues" );
  numSubQueues = pOptionsNucleus->getAsInt( key.c_str(), DEF_NUM_SUBQUEUES );
  key.assign( baseName ); key.append( "maxEventsInSeqFromMainQueue" );
  maxEventsFromMainQueue = pOptionsNucleus->getAsInt( key.c_str(), DEF_NUM_EVENTS_IN_SEQ_FROM_MAIN_QUEUE );
  key.assign( baseName ); key.append( "maxEventsInSeqFromSubQueue" );
  maxEventsFromSubQueue = pOptionsNucleus->getAsInt( key.c_str(), DEF_NUM_EVENTS_IN_SEQ_FROM_SUB_QUEUE );
  numTimesReadFromMainQueue = 0;
  bFeedingFromMainQueue = true;
  listSize = 0;
  log.info( log.LOGALWAYS, "init: queue:'%s' maxEventsInSeqFromMainQueue:%d maxEventsInSeqFromSubQueue:%d numSubQueues:%d", queueName.c_str(), maxEventsInSeqFromMainQueue, maxEventsInSeqFromSubQueue, numSubQueues );
} // init

/**
 * **/
std::string& batchQueue::getStatus( )
{
  baseQueue::getStatus();
//  char str[64];
//  sprintf( str, "l:%u e:%u", availableQueues.size(), subListSize );
//  statusStr.append( str );
  return statusStr;
} // getStatus

/**
 * retrieves the next event from a queue to be executed
 * @return the event or NULL if there are none
 * **/
baseEvent* batchQueue::popAvailableEvent( )
{
  if( listSize == 0 ) return NULL;
  baseEvent* pEvent = NULL;

  do
  {
    if( bFeedingFromMainQueue )
    {
      pEvent = getEventFromMainQueue();
      if( pEvent != NULL )
      {
        pEvent = checkIfEventIsExpired( pEvent );
        // TODO - check if this should not precede the previous - alternatively
        // the previous should also update the queued stats
        // updateQueuedStats( pEvent );

        numTimesReadFromMainQueue++;
        if( numTimesReadFromMainQueue >= maxEventsFromMainQueue )
        {
          bFeedingFromMainQueue = false;
          numTimesReadFromMainQueue = 0;
        } // if
      } // if
      else
      {
        bFeedingFromMainQueue = false;
        numTimesReadFromMainQueue = 0;
      } // else
    } // if bFeedingFromMainQueue
    else
    {
      pEvent = getEventFromSubQueue();
      if( pEvent != NULL )
      {
        pEvent = checkIfEventIsExpired( pEvent );
        // TODO - check if this should not precede the previous - alternatively
        // the previous should also update the queued stats
        // updateQueuedStats( pEvent );

        numTimesReadFromSubQueue++;
        if( numTimesReadFromSubQueue >= maxEventsFromSubQueue )
        {
          bFeedingFromMainQueue = true;
          numTimesReadFromSubQueue = 0;
        } // if
      } // if
      else
      {
        bFeedingFromMainQueue = true;
        numTimesReadFromSubQueue = 0;
      } // else
    } // else bFeedingFromMainQueue
  } while( (pEvent == NULL) && (listSize > 0) );

  // updateQueuedStats( pEvent );
  if( pEvent == NULL ) log.info( log.MIDLEVEL, "popAvailableEvent: queue:'%s' only had expired events", queueName.c_str() );
  return pEvent;
} // popAvailableEvent

/**
 * pops a single event from the eventList
 * @param the queue from which to retrieve
 * @return the next event on the queue or NULL
 * **/
baseEvent* batchQueue::popEvent( straightQueueT* theQueue )
{
  if( theQueue->empty() ) return NULL;
  baseEvent* pEvent = theQueue->back();
  theQueue->pop_back();
  listSize--;
  return pEvent;
} // popEvent

/**
 * drop a queue - assume it is already removed from commonQueue or batchOnlyQueue
 * it either contained a single event and was still in commonQueue or it contains
 * multiple events and was in batchOnlyQueue
 * @exception on error
 * **/
void batchQueue::dropQueue( straightQueueT* pQueue, bool bDropMapLookup, bool bDropMap )
{
  try
  {
    unsigned int key = getKeyForQueue( pQueue );
    key = it->second;
    if( bDropMapLookup )
      batchMapLookup.erase( it );

    if( bDropMap )
    {
      batchMapIteratorT it1;
      it1 = batchMap.find( key );
      if( it1 != batchMap.end() )
        batchMap.erase( it1 );
      else
        log.error( "dropQueue: queue:%p key:%d failed to find in batchMap", pQueue, key );
    } // if bDropMap
    log.info( log.LOGNORMAL, "dropQueue: queue:%p bDropMapLookup:%d bDropMap:%d", pQueue, bDropMapLookup, bDropMap );
  } // try
  catch( Exception e )
  {
    log.error( "dropQueue: failed for queue:%p", pQueue );
  } // catch
} // dropQueue

/**
 * get the next event from the main queue
 * @return the event or NULL if empty
 * **/
baseEvent* batchQueue::getEventFromMainQueue( )
{
  baseEvent* pEvent = NULL;

  // the easy case is if mainQueue is not empty
  // mainQueue only ever has events if these got removed from commonQueue
  // in an attempt to find new candidates for batchOnlyQueue
  if( !mainQueue.empty() )
  {
    pEvent = popEvent( &mainQueue );
    return pEvent;
  } // if
  
  // we now pop events from the common queue if there are any.
  // the first single event (single event in a batch queue) will be returned.
  // all multi-event batches encountered before the first single event queue will
  // be stuck into the batchOnlyQueue
  if( commonQueue.empty() ) return NULL;
  while( (pEvent==NULL) && !commonQueue.empty() )
  {
    straightQueueT* pQueue = commonQueue.back();
    commonQueue.pop_back();
    unsigned int key = getKeyForQueue( pQueue );

    // all queues should contain 1 or more events
    if( (pQueue.size()==1) || (key==0) )
    {
      // retrieve the singular event (or multiple events if queue 0) and drop the queue
      pEvent = popEvent( pQueue );
      // queue 0 is the main queue and could have a number of events associated with it
      if( !pQueue->empty() )
      {
        while( !pQueue->empty() )
        {
          baseEvent* pEvent1;
          pEvent1 = popEvent( pQueue );
          mainQueue.push_front( pEvent1 );
        } // while
      } // if
      dropQueue( pQueue, true, true );
      delete pQueue;
    } // if
    else
    {
      // place on the batch only queue
      batchOnlyQueue.push_front( pQueue );
    } // else
  } // while
  return pEvent;
} // getEventFromMainQueue

/**
 * get the next event from the sub queues
 * @return the event or NULL if empty
 * **/
baseEvent* batchQueue::getEventFromSubQueue( )
{
  baseEvent* pEvent = NULL;

  // make sure we run the configured number of round-robin queues if possible
  while( (subQueues.size()<numSubQueues) && (!batchOnlyQueue.empty()||!commonQueue.empty()) )
  {
    // if there are batch queues in the batchOnlyQueue retrieve from here
    if( !batchOnlyQueue.empty() )
    {
      straightQueueT* pQueue = batchOnlyQueue.back();
      batchOnlyQueue.pop_back();
      subQueues.push_front( pQueue );
    } // if
    else
    {
      straightQueueT* pQueue = commonQueue.back();
      commonQueue.pop_back();
      unsigned int key = getKeyForQueue( pQueue );
      if( (pQueue.size()>1) && (key!=0) )
      {
        // place into the sub queues list
        subQueues.push_front( pQueue );
      } // if
      else
      {
        // transfer the contents of the queue to the main queue
        while( !pQueue->empty() )
        {
          baseEvent* pEvent1;
          pEvent1 = popEvent( pQueue );
          mainQueue.push_front( pEvent1 );
        } // while
        dropQueue( pQueue, true, true );
        delete pQueue;
      } // else
    } // else
  } // while subQueues.size()

  if( subQueues.empty() )
    return NULL;

  // grab the next subqueue
  straightQueueT* pQueue = subQueues.back();
  subQueues.pop_back();

  // retrieve an event
  pEvent = popEvent( pQueue );

  // place the queue on the back of the subQueues queue again
  if( !pQueue.empty() )
  {
    subQueues.push_front( pQueue );
  } // if
  else
  {
    log.info( log.LOGNORMAL, "getEventFromSubQueue: deleting queue:%p", pQueue );
    dropQueue( pQueue, true, true );
    delete pQueue;
  } // else

  return pEvent;
} // getEventFromSubQueue

/**
 * queue a new event
 * **/
void batchQueue::queueEvent( baseEvent* pEvent )
{
  // set the queue time
  // calculate expiry time if requested
  pEvent->setQueueTime( now );
  int lifetime = pEvent->getLifetime( );
  if( lifetime != -1 )
    pEvent->setExpiryTime( lifetime + now );

  checkQueueOverflow( );
  unsigned int batchQueueKey = pEvent->getSubQueue();

  // find an existing batchQueue if any
  straightQueueT* pBatchQueue = findExistingQueue( batchQueueKey );

  // if no existing batchQueue create one and insert it into the map and commonQueue
  if( pBatchQueue == NULL )
    pBatchQueue = createBatchQueue( batchQueueKey );
  
  // queue the event on the applicable queue
  pBatchQueue->push_front( pEvent );
  listSize++;

  log.info( log.MIDLEVEL ) << "queueEvent: queue:'" << queueName << "' qlen:" << listSize << " queued event: " << pEvent->toString( );
} // queueEvent

/**
 * find the existing batch queue
 * @param key
 * **/
straightQueueT* batchQueue::findExistingQueue( int key )
{
  straightQueueT* theQueue = NULL;
  batchMapIteratorT it;
  it = batchMap.find( key );
  if( it != batchMap.end() )
    theQueue = it->second;
  return theQueue;
} // findExistingQueue

/**
 * find key for queue
 * @param pQueue
 * @param bThrowOnNotFound
 * @return the key or 0 on not found unless bThrowOnNotFound
 * @exception on not found and bThrowOnNotFound
 * **/
unsigned int batchQueue::getKeyForQueue( straightQueueT* pQueue, bool bThrowOnNotFound )
{
  unsigned int key = 0;
  batchMapLookupIteratorT it;
  it = batchMapLookup.find( pQueue );
  if( it != batchMapLookup.end() )
  {
    key = it->second;
  } // if
  else
  {
    if( bThrowOnNotFound )
      throw new Exception( log, log.WARN, "getKeyForQueue: key not found for queue:%p", pQueue );
    else
      log.warn( log.LOGALWAYS, "getKeyForQueue: key not found for queue:%p", pQueue );
  } // else
  return key;
} // getKeyForQueue

/**
 * creates a new batch queue and insert it into the map and commonQueue
 * @param key
 * **/
straightQueueT* batchQueue::createBatchQueue( int key )
{
  straightQueueT* theQueue = new straightQueueT;
  commonQueue.push_front( theQueue );
  batchMap.insert( batchMapPairT(key,theQueue) );
  batchMapLookup.insert( batchMapLookupPairT(theQueue,key) );
  return theQueue;
} // createBatchQueue

/**
 * checks overflow on queue - if so dump to the recoveryLog
 * **/
void batchQueue::checkQueueOverflow()
{
  if( bRecoveryProcess ) return;  // allow the queue to take all the events for recovery
  if( listSize > maxQueueLength )
    dumpList( "overflow" );
} // checkQueueOverflow

/**
 * dumps the list to the recovery log
 * expired events are not dumped
 * @param reason
 * **/
void batchQueue::dumpList( const char* reason )
{
  int numProcessed = dumpQueue( &mainQueue, reason );

  while( !subQueues.empty() )
  {
    // grab the next subqueue
    straightQueueT* pQueue = subQueues.back();
    subQueues.pop_back();
    numProcessed += dumpQueue( pQueue, reason );

    log.info( log.LOGNORMAL, "dumpList: deleting queue:%p", pQueue );
    dropQueue( pQueue, true, true );
    delete pQueue;
  } // while

  numProcessed += dumpQueue( &batchOnlyQueue, reason );
  numProcessed += dumpQueue( &commonQueue, reason );
  listSize -= numProcessed;
  
  log.warn( log.LOGALWAYS, "dumpList: queue '%s' processed %d entries listSize:%d, reason:'%s'", queueName.c_str(), numProcessed, listSize, reason );
} // dumpList

/**
 * dump stats into an http object
 * **/
void batchQueue::dumpHttp( dispatchNotifyEvent* pEvent )
{
//  baseQueue::dumpHttp( pEvent );
//  pEvent->addParam( "delayqlen", delayListSize );
} // dumpHttp

