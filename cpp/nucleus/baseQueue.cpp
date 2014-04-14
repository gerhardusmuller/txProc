/**
 Base queue class - owns a workerPool and a baseQueue derived object
 
 $Id: baseQueue.cpp 2629 2012-10-19 16:52:17Z gerhardus $
 Versioning: a.b.c a is a major release, b represents changes or new features, c represents bug fixes. 
 @version 1.0.0		10/09/2009		Gerhardus Muller		Script created
 @version 1.1.0		20/10/2010		Gerhardus Muller		split per queue logging into its own file

 @note

 @todo
 
 @bug

	Copyright Notice
 * **/

#include "nucleus/baseQueue.h"
#include "nucleus/baseEvent.h"
#include "nucleus/recoveryLog.h"
#include "nucleus/optionsNucleus.h"
#include "nucleus/queueContainer.h"
#include "src/options.h"

/**
 * constructor
 * **/
baseQueue::baseQueue( tQueueDescriptor* theDescriptor, recoveryLog* theRecoveryLog, bool bRecovery, const char* objName )
  : object( objName ),
    bRecoveryProcess( bRecovery ),
    pRecoveryLog( theRecoveryLog )
{
  std::string logfile = pOptions->logBaseDir;
  logfile.append( "q_" );
  logfile.append( theDescriptor->name );
  logfile.append( ".log" );
  log.info( log.LOGALWAYS, "init: queue:%s changing logging to '%s'", theDescriptor->name.c_str(), logfile.c_str() );
  log.instanceOpenLogfile( logfile.c_str(), pOptionsNucleus->bFlushLogs );
  char tmp[64];
  sprintf( tmp, "%s-%s", objName, theDescriptor->name.c_str() );
  log.setInstanceName( tmp );

  queueName = theDescriptor->name;
  optionsKey = theDescriptor->key;
  maxQueueLength = theDescriptor->maxLength;
  init();
} // baseQueue

/**
 * destructor
 * **/
baseQueue::~baseQueue()
{
} // ~baseQueue

/**
 * init
 * **/
void baseQueue::init( )
{
  log.setAddPid( true );

  resetStats( );
} // init

/**
 * resets the stats
 * **/
void baseQueue::resetStats( )
{
  numExpiredEvents = 0;
} // resetStats

/**
 * dump stats into an http object
 * **/
void baseQueue::dumpHttp( baseEvent* pEvent )
{
} // dumpHttp

/**
 * send a result message - it is not regarded as an error if there is no returnFd specified
 * do not send result messages if it is a recovery process - the fd in the event is not
 * valid
 * TODO code the dispatchResultEvent equivalent
 *
 * @param pEvent - the source event
 * @param bSuccess - result of the execution
 * @param result - string result
 * **/
void baseQueue::sendResult( baseEvent* pEvent, bool bSuccess, const std::string& result, const std::string& errorString, const std::string& traceTimestamp, const std::string& failureCause, const std::string& systemParam )
{
  int returnFd = pEvent->getReturnFd( );
  if( (returnFd != -1) && !bRecoveryProcess && !pEvent->hasBeenExpired())
  {
    pEvent->shiftReturnFd();  // drop the return fd that we have just used
//    dispatchResultEvent* pReturn = new dispatchResultEvent( NULL, bSuccess, result, pEvent->getRef(), pEvent->getEventClass(), errorString, traceTimestamp, failureCause, systemParam );
//    pReturn->trace = pEvent->trace;
//    pReturn->setReturnFd( pEvent->getFullReturnFd() );
    
//    pReturn->serialise( returnFd, FROM );   // we do not in general know to whom we are responding - neither should recovery be necessary
//    delete pReturn;
  } // if
} // sendResult

/**
 * dumps a list to the recovery log
 * @param pQueue
 * @param reason
 * @return the number of events dumped
 * **/
int baseQueue::dumpList( straightQueueT* pList, const char* reason )
{
  int count = 0;
  int numProcessed = 0;
  baseEvent* pEvent = NULL;
  while( !pList->empty() )
  {
    pEvent = pList->back();
    pList->pop_back();
    numProcessed++;
    if( !pEvent->hasBeenExpired() )
    {
      // int timeInQueue = now - pEvent->getQueueTime( );

      // record the stats
      // accQueueTime += timeInQueue;
      // if( timeInQueue > maxQueueTime ) maxQueueTime = timeInQueue;
      // countQueueEvents++;

      // write to the recovery log
      if( !pEvent->isExpired( now ) )
      {
        pRecoveryLog->writeEntry( pEvent, reason, queueName.c_str() );
        sendResult( pEvent, false, std::string(), std::string(), std::string(), std::string("dumped"), std::string() );
        count++;
      }
      else
      {
        numExpiredEvents++;
        sendResult( pEvent, false, std::string(), std::string(), std::string(), std::string("expired"), std::string() );
        log.info( log.LOGMOSTLY ) << "dumpQueue: expired event: " << pEvent->toString();
      }
    } // if !hasBeenExpired
    delete pEvent;
  } // while( !priorityList.empty

  // TODO
  //log.warn( log.LOGALWAYS, "dumpList: queue:'%s' processed %d entries, dumped %d expired events, reason '%s', acc queue time %d, numStatEvents %d, max queue time %d, mean queue time %f", queueName.c_str(), count, numExpiredEvents, reason, accQueueTime, countQueueEvents, maxQueueTime, (countQueueEvents>0)?(float)accQueueTime/countQueueEvents:0);
  log.warn( log.LOGALWAYS, "dumpList: queue:'%s' processed %d entries, dumped %d expired events, reason '%s'", queueName.c_str(), count, numExpiredEvents, reason );
  return numProcessed;
} // dumpList

/**
 * checks if the event has been expired or is expired; deletes
 * it and generates the appropriate notification
 * @param pEvent
 * @return pEvent or NULL if it was expired
 * **/
baseEvent* baseQueue::checkIfEventIsExpired( baseEvent* pEvent )
{
  if( pEvent == NULL ) return NULL;

  if( pEvent->hasBeenExpired() )
  { // remove and discard
    delete pEvent;
    pEvent = NULL;
  } // if hasBeenExpired
  else if( pEvent->isExpired(now) )
  {
    sendResult( pEvent, false, std::string(), std::string(), std::string(), std::string("expired"), std::string() );
    pEvent->expire();
    numExpiredEvents++;
    log.warn( log.LOGMOSTLY ) << "checkIfEventIsExpired: queue:'" << queueName << "' expired event (queued for " << (now-pEvent->getQueueTime()) << "s lag " << (now-pEvent->getExpiryTime()) << "s): " << pEvent->toString();
    delete pEvent;
    pEvent = NULL;
  } // else if

  return pEvent;
} // checkIfEventIsExpired


