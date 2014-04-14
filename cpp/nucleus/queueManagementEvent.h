/**
 handles the generation of queue management events
 
 $Id: queueManagementEvent.h 2622 2012-10-11 15:24:56Z gerhardus $
 Versioning: a.b.c a is a major release, b represents changes or new features, c represents bug fixes. 
 @version 1.0.0		30/08/2012		Gerhardus Muller		Script created

 @note

 @todo
 
 @bug

	Copyright Notice
 * **/

#if !defined( queueManagementEvent_defined_ )
#define queueManagementEvent_defined_

#include "utils/object.h"
#include "nucleus/baseEvent.h"

class urlRequest;
class scriptExec;
class workerDescriptor;
struct tQueueDescriptor;

class queueManagementEvent : public object
{
  // Definitions
  public:
  /**
   * QMAN_NONE
   * QMAN_PSTARTUP  - persistent process startup; carries category,ownQueue,type,workerPid,childPid,scriptCmd
   * QMAN_DONE      - not implemented
   * QMAN_PDIED     - persistent process died - mainly as a result of it exiting; carries category,ownQueue,type,workerPid,childPid,scriptCmd,exitStatus,termSignal,errorString,failureCause
   * QMAN_WSTARTUP  - worker process startup, shutdown, dying, respawning
   * QMAN_PSTARTUP,QMAN_PDIED carries similar information to QMAN_WSTARTUP but is collected at different points
   * QMAN_PSTARTUP,QMAN_PDIED are generated from the worker process and carries information on the actual termination
   *   of the script including its exit code.  Unknown at this point as to what happens if it is killed do resources over usage
   *   the workerPid can be used to track the same worker instance as that only changes if the actual worker process gets killed (which may as a result of resource usage)
   * QMAN_WSTARTUP provides similar information but from the level of the queue worker pool
   * **/
  enum eQManagementType { QMAN_NONE=0,QMAN_PSTARTUP=1,QMAN_DONE=2,QMAN_PDIED=4,QMAN_WSTARTUP=8 };

  // Methods
  public:
  queueManagementEvent();
  queueManagementEvent( urlRequest* theUrlRequest, scriptExec* theScriptExec, tQueueDescriptor* theContainerDesc, int theNucleusFd );
  queueManagementEvent( workerDescriptor* theWorker, tQueueDescriptor* theContainerDesc, int theNucleusFd );
  queueManagementEvent( tQueueDescriptor* theContainerDesc, int theNucleusFd );
  virtual ~queueManagementEvent();
  void genEvent( eQManagementType type, baseEvent* pEvent=NULL );
  void genEvent( eQManagementType type, int oldPid, int newPid );
  static const char* eQManagementTypeToStr( eQManagementType type );
  static eQManagementType eQManagementStrToType( const std::string& str );

  private:

  protected:

  // Properties  
  public:
  static logger               staticLogger;         ///< class scope logger

  protected:
  urlRequest*                 pUrlRequest;          ///< object used for URL requests / notifications
  scriptExec*                 pScriptExec;          ///< object used to exec scripts
  tQueueDescriptor*           pContainerDesc;       ///< container descriptor
  workerDescriptor*           pWorker;              ///< the workerDescriptor class
  int                         nucleusFd;            ///< nucleus process file descriptor

  private:
};	// class queueManagementEvent

#endif // !defined( queueManagementEvent_defined_)
