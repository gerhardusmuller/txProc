/**
 Standard queue class
 
 $Id: straightQueue.cpp 2546 2012-08-28 20:54:42Z gerhardus $
 Versioning: a.b.c a is a major release, b represents changes or new features, c represents bug fixes. 
 @version 1.0.0		10/09/2009		Gerhardus Muller		Script created

 @note

 @todo
 
 @bug

	Copyright Notice
 * **/

#include "nucleus/straightQueue.h"
#include "nucleus/optionsNucleus.h"
#include "nucleus/recoveryLog.h"
#include "nucleus/baseEvent.h"

const char *const straightQueue::FROM = typeid( straightQueue ).name();

/**
 * constructor
 * **/
straightQueue::straightQueue( tQueueDescriptor* theDescriptor, recoveryLog* theRecoveryLog, bool bRecovery, const char* objName )
  : baseQueue( theDescriptor, theRecoveryLog, bRecovery, "straightQueue" )
{
  listSize = 0;
  bExitWhenDone = false;
  init();
} // straightQueue

/**
 * destructor
 * **/
straightQueue::~straightQueue( )
{
  log.info( log.MIDLEVEL, "~straightQueue:'%s' %d (%d) remaining events", queueName.c_str(), eventList.size(), listSize );
} // ~straightQueue

/**
 * init
 * **/
void straightQueue::init( )
{
  log.setAddPid( true );
  log.info( log.LOGMOSTLY, "init: queue '%s', maxLength %d", queueName.c_str(), maxQueueLength );
} // init

/**
 * retrieves the next event from the queue to be executed
 * @param fd is ignored for the straight queue
 * @return the event or NULL if there are none
 * **/
baseEvent* straightQueue::popAvailableEvent( int fd )
{
  if( !bExitWhenDone && (listSize==0) ) return NULL;

  // feed persistent apps with end of queue commands so that they can detect the 
  // end of the queue when it is empty
  if( bExitWhenDone && (listSize==0) )
  {
    baseEvent* pCommand = new baseEvent( baseEvent::EV_COMMAND );
    pCommand->setCommand( baseEvent::CMD_END_OF_QUEUE );
    log.info( log.LOGMOSTLY, "popAvailableEvent: generating a CMD_END_OF_QUEUE" );
    return pCommand;
  } // if

  // retrieve an event from the queue
  baseEvent* pEvent = NULL;
  do
  {
    pEvent = popEvent();
    pEvent = checkIfEventIsExpired( pEvent );
  } while( (pEvent == NULL) && !eventList.empty() );

  // updateQueuedStats( pEvent );
  if( pEvent == NULL )
    log.info( log.MIDLEVEL, "popAvailableEvent: queue:'%s' only had expired events", queueName.c_str() );
  else
    log.info( log.MIDLEVEL, "popAvailableEvent: qlen:%d", listSize );
  return pEvent;
} // popAvailableEvent

/**
 * queue a new event
 * **/
void straightQueue::queueEvent( baseEvent* pEvent )
{
  checkQueueOverflow( );

  // now queue
  eventList.push_front( pEvent );
  listSize++;
  log.info( log.MIDLEVEL ) << "queueEvent: queue:'" << queueName << "' qlen:" << listSize << " queued event: " << pEvent->toString( );
} // queueEvent

/**
 * pops a single event from the eventList
 * @param the queue from which to retrieve
 * @return the next event on the queue or NULL
 * **/
baseEvent* straightQueue::popEvent( )
{
  if( eventList.empty() ) return NULL;
  baseEvent* pEvent = eventList.back();
  eventList.pop_back();
  listSize--;
  return pEvent;
} // popEvent

/**
 * checks overflow on queue - if so dump to the recoveryLog
 * **/
void straightQueue::checkQueueOverflow()
{
  if( bRecoveryProcess ) return;  // allow the queue to take all the events for recovery
  if( listSize >= maxQueueLength )
    dumpQueue( "overflow" );
} // checkQueueOverflow

/**
 * dumps the list to the recovery log
 * expired events are not dumped
 * @param reason
 * **/
void straightQueue::dumpQueue( const char* reason )
{
  int numProcessed = dumpList( &eventList, reason );
  listSize -= numProcessed;
} // dumpQueue

/**
 * scan list for expired events and expire these
 * **/
void straightQueue::scanForExpiredEvents( )
{
  straightQueueIteratorT p = eventList.begin();
  while( p != eventList.end() )
  {
    // only expire once - it is inefficient to try and remove expired events from the queue and therefor they
    // are left in the queue in an expired state and will be discarded when trying to feed them
    if( !(*p)->hasBeenExpired() )
    {
      if( (*p)->isExpired(now) )
      {
        sendResult( *p, false, std::string(), std::string(), std::string(), std::string("expired"), std::string() );
        (*p)->expire();
        numExpiredEvents++;
        log.warn( log.LOGMOSTLY ) << "scanForExpiredEvents: queue '" << queueName << "' expired event (queued for " << (now-(*p)->getQueueTime()) << "s lag " << (now-(*p)->getExpiryTime()) << "s): " << (*p)->toString();
      } // if
      else if( log.wouldLog( log.LOGSELDOM ) )
      {
        log.debug( log.LOGMOSTLY ) << "scanForExpiredEvents: queue '" << queueName << "' event ok (queued for " << (now-(*p)->getQueueTime()) <<  "s): " << (*p)->toString();
      } // else if
    } // if !hasBeenExpired
    p++;
  } // while
} // scanForExpiredEvents

/**
 * produces a csv version of the queue status and statistics
 * **/
std::string& straightQueue::getStatus( )
{
  char str[128];
  sprintf( str, "%u,%u,%d", listSize, maxQueueLength, numExpiredEvents );
  
  resetStats();
  statusStr = str;
  return statusStr;
} // getStatus

/**
 * returns the key to getStatus - typically used as a heading to the csv it is stored in
 * **/
std::string& straightQueue::getStatusKey( )
{
  statusStrKey = "qSize,qMax,numExp";
  return statusStrKey;
} // getStatusKey
