/**
 handles the generation of queue management events
 
 $Id: queueManagementEvent.cpp 2622 2012-10-11 15:24:56Z gerhardus $
 Versioning: a.b.c a is a major release, b represents changes or new features, c represents bug fixes. 
 @version 1.0.0		30/08/2012		Gerhardus Muller		Script created

 @note

 @todo
 
 @bug

	Copyright Notice
 * **/

#include "nucleus/queueManagementEvent.h"
#include "nucleus/workerDescriptor.h"
#include "nucleus/urlRequest.h"
#include "nucleus/scriptExec.h"

logger queueManagementEvent::staticLogger = logger( "queueManagementEventS", loggerDefs::MIDLEVEL );

/**
 * constructor
 * **/
queueManagementEvent::queueManagementEvent( )
  : object( "queueManagementEvent" )
{
  pUrlRequest = NULL;
  pScriptExec = NULL;
  pContainerDesc = NULL;
  pWorker = NULL;
  nucleusFd = -1;
} // queueManagementEvent

queueManagementEvent::queueManagementEvent( urlRequest* theUrlRequest, scriptExec* theScriptExec, tQueueDescriptor* theContainerDesc, int theNucleusFd )
  : object( "queueManagementEvent" ),
  pUrlRequest(theUrlRequest),
  pScriptExec(theScriptExec),
  pContainerDesc(theContainerDesc),
  nucleusFd(theNucleusFd)
{
  pWorker = NULL;
} // queueManagementEvent

queueManagementEvent::queueManagementEvent( workerDescriptor* theWorker, tQueueDescriptor* theContainerDesc, int theNucleusFd )
  : object( "queueManagementEvent" ),
  pContainerDesc(theContainerDesc),
  pWorker(theWorker),
  nucleusFd(theNucleusFd)
{
  pUrlRequest = NULL;
  pScriptExec = NULL;
  pContainerDesc = NULL;
} // queueManagementEvent

queueManagementEvent::queueManagementEvent( tQueueDescriptor* theContainerDesc, int theNucleusFd )
  : object( "queueManagementEvent" ),
  pContainerDesc(theContainerDesc),
  nucleusFd(theNucleusFd)
{
  pUrlRequest = NULL;
  pScriptExec = NULL;
  pWorker = NULL;
} // queueManagementEvent

/**
 * destructor
 * **/
queueManagementEvent::~queueManagementEvent( )
{
} // ~queueManagementEvent

/**
 * generates a management event - these events are for persistent apps
 * scriptExec.cpp: if( pQueueManagement != NULL ) pQueueManagement->genEvent( queueManagementEvent::QMAN_PSTARTUP );
 * worker.cpp:     pQueueManagement->genEvent( queueManagementEvent::QMAN_PDIED );
 * **/
void queueManagementEvent::genEvent( eQManagementType type, baseEvent* pEvent )
{
  if( (pContainerDesc==NULL) ) throw Exception( log, log.WARN, "genEvent pContainerDesc is NULL" );
  // check if the user requested an event of this type
  if( !(pContainerDesc->managementEvents & type) || pContainerDesc->managementQueue.empty() ) return;

  baseEvent* pManage = new baseEvent( pContainerDesc->managementEventType );
  pManage->setDestQueue( pContainerDesc->managementQueue );
  pManage->setRef( log.getTimestamp() );
  if( pContainerDesc->managementEventType == baseEvent::EV_URL )
  {
    pManage->addParam( "category", "queueManagementEvent" );
    pManage->addParam( "ownQueue", pContainerDesc->name );
    pManage->addParam( "type", eQManagementTypeToStr( type ) );
    if( pWorker != NULL ) pManage->addParam( "workerPid", pWorker->getPid() );
    if( pScriptExec != NULL )
    {
      pManage->addParam( "workerPid", pScriptExec->getWorkerPid() );        // carries the wpid - ie parent of childPid
      pManage->addParam( "scriptCmd", pScriptExec->getScriptCmd() );
      if( type == QMAN_PDIED )
      {
        pManage->addParam( "childPid", pScriptExec->getExitedChildPid() );  // carries the pid of the scriptCmd
        pManage->addParam( "exitStatus", pScriptExec->getExitStatus() );    // should be 0 for a process that exited with code 0
        pManage->addParam( "termSignal", pScriptExec->getTermSignal() );    // signal that killed the process or 0 if none
        std::string err = pScriptExec->getErrorString();
        if( !err.empty() ) pManage->addParam( "errorString", err );
        std::string cause = pScriptExec->getFailureCause();
        if( !cause.empty() ) pManage->addParam( "failureCause", cause );
      } // if
      else
        pManage->addParam( "childPid", pScriptExec->getChildPid() );        // carries the pid of the scriptCmd
    } // if
  } // if
  else
  {
    pManage->addScriptParam( "queueManagementEvent" );
    pManage->addScriptParam( pContainerDesc->name );
    pManage->addScriptParam( eQManagementTypeToStr( type ) );
    if( pWorker != NULL )
      pManage->addScriptParam( pWorker->getPid() );
    else
      pManage->addScriptParam( 0 );   // workerPid
    if( pScriptExec != NULL )
    {
      pManage->addScriptParam( pScriptExec->getWorkerPid() );             // this should be the same as the preceding parameter but only one of the two would be set as access to the workerDescriptor object and the scriptExec object is mutually exclusive
      if( type == QMAN_PDIED )
        pManage->addScriptParam( pScriptExec->getExitedChildPid() );      // carries the pid of the scriptCmd
      else
        pManage->addScriptParam( pScriptExec->getChildPid() );
      pManage->addScriptParam( pScriptExec->getScriptCmd() );
      pManage->addScriptParam( pScriptExec->getErrorString() );
      pManage->addScriptParam( pScriptExec->getFailureCause() );
    } // if
    else
    {
      pManage->addScriptParam( 0 );
      pManage->addScriptParam( 0 );
      pManage->addScriptParam( "unknown" );
      pManage->addScriptParam( "unknown" );
      pManage->addScriptParam( "unknown" );
    } // else
  } // else

  pManage->serialise( nucleusFd );
} // genEvent

/**
 * generates a management event
 * the QMAN_WSTARTUP events are to be used with the collectionQueue
 * workerPool.cpp: pQueueManagement->genEvent(queueManagementEvent::QMAN_WSTARTUP, 0, pid ;           initial creation
 * workerPool.cpp: pQueueManagement->genEvent(queueManagementEvent::QMAN_WSTARTUP, childPid, newPid ; respawning after death
 * workerPool.cpp: pQueueManagement->genEvent(queueManagementEvent::QMAN_WSTARTUP, childPid, 0);      killing off the child due to shutdown or queue worker pool size reduction
 * the newPid is the same as the workerPid in QMAN_PSTARTUP or the wpid field if the collection queue is used - this
 * allows the wpid to be matched to the actual pid of the script executing
 * **/
void queueManagementEvent::genEvent( eQManagementType type, int oldPid, int newPid )
{
  // check if the user requested an event of this type
  if( !(pContainerDesc->managementEvents & type) || pContainerDesc->managementQueue.empty() ) return;

  baseEvent* pManage = new baseEvent( pContainerDesc->managementEventType );
  pManage->setDestQueue( pContainerDesc->managementQueue );
  pManage->setRef( log.getTimestamp() );
  if( pContainerDesc->managementEventType == baseEvent::EV_URL )
  {
    pManage->addParam( "category", "queueManagementEvent" );
    pManage->addParam( "ownQueue", pContainerDesc->name );
    pManage->addParam( "type", eQManagementTypeToStr( type ) );
    pManage->addParam( "oldPid", oldPid );
    pManage->addParam( "newPid", newPid );                                // is the same as wpid
  } // if
  else
  {
    pManage->addScriptParam( "queueManagementEvent" );
    pManage->addScriptParam( pContainerDesc->name );
    pManage->addScriptParam( eQManagementTypeToStr( type ) );
    pManage->addScriptParam( oldPid );
    pManage->addScriptParam( newPid );
  } // else

  pManage->serialise( nucleusFd );
} // genEvent

const char* queueManagementEvent::eQManagementTypeToStr( eQManagementType type )
{
  switch( type )
  {
    case QMAN_NONE:
      return "QMAN_NONE";
      break;
    case QMAN_PSTARTUP:
      return "QMAN_PSTARTUP";
      break;
    case QMAN_DONE:
      return "QMAN_DONE";
      break;
    case QMAN_PDIED:
      return "QMAN_PDIED";
      break;
    case QMAN_WSTARTUP:
      return "QMAN_WSTARTUP";
      break;
    default:
        return "eQManagementTypeToStr type not defined";
  } // switch 
} // eQManagementTypeToStr

queueManagementEvent::eQManagementType queueManagementEvent::eQManagementStrToType( const std::string& str )
{
  if( str.compare("QMAN_WSTARTUP") == 0 )
    return QMAN_WSTARTUP;
  else if( str.compare("QMAN_PDIED") == 0 )
    return QMAN_PDIED;
  else if( str.compare("QMAN_DONE") == 0 )
    return QMAN_DONE;
  else if( str.compare("QMAN_PSTARTUP") == 0 )
    return QMAN_PSTARTUP;
  else if( str.compare("QMAN_NONE") == 0 )
    return QMAN_NONE;
  else
  {
    staticLogger.warn( staticLogger.LOGALWAYS, "eQManagementStrToType type:'%s' not recognised", str.c_str() );
    return QMAN_NONE;
  } // else
} // queueManagementEvent
