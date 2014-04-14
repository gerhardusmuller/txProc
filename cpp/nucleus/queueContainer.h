/**
 Queue container class - owns a workerPool and a queueContainer derived object
 
 $Id: queueContainer.h 2890 2013-06-20 15:59:42Z gerhardus $
 Versioning: a.b.c a is a major release, b represents changes or new features, c represents bug fixes. 
 @version 1.0.0		16/09/2009		Gerhardus Muller		Script created
 @version 1.1.0		30/03/2011		Gerhardus Muller		added bBlockingWorkerSocket to tQueueDescriptor

 @note

 @todo
 
 @bug

	Copyright Notice
 * **/

#if !defined( queueContainer_defined_ )
#define queueContainer_defined_

#include "utils/object.h"
#include "nucleus/baseEvent.h"
#include "nucleus/workerPool.h"
#include "nucleus/baseQueue.h"

class recoveryLog;
class baseEvent;
class queueContainer;

struct tQueueDescriptor
{
  std::string               name;
  std::string               type;
  std::string               key;
  int                       numWorkers;               // default DEF_NUM_QUEUE_WORKERS
  int                       maxLength;                // default DEF_MAX_QUEUE_LEN
  int                       maxExecTime;              // default 0
  int                       parseResponseForObject;   // default 1
  bool                      bRunPriviledged;          // if true the worker will not drop its priviledges permanently - default false
  bool                      bBlockingWorkerSocket;    // if true use a blocking socket to communicate with the worker - default false
  std::string               persistentApp;            // persistent application to execute
  std::string               defaultScript;            // default script if the EV_PERL etc specifies none
  std::string               defaultUrl;               // default URL to use if the EV_URL specifies none
  std::string               errorQueue;               // for failures place a copy of the event on this queue with its type changed to EV_ERROR rather than generating a recovery event
  std::string               managementQueue;          // queue to send management events to - if undefined will not be generated
  baseEvent::eEventType     managementEventType;      // EV_SCRIPT,EV_PERL,EV_BIN,EV_URL - defaults to EV_PERL
  unsigned int              managementEvents;         // which events to generate - xor a list any number of queueManagementEvent::eQManagementType values
  queueContainer*           pQueue;
  std::string               statsFile;
  std::string               statsDir;
  int                       statsFd;
  std::vector<int>          fdsToRemainOpen;          // list of additional file descriptors to leave open
};

class queueContainer : public object
{
  // Definitions
  public:
  static const int DEF_MAX_QUEUE_LEN = 500000;
  static const int DEF_NUM_QUEUE_WORKERS = 2;

  // Methods
  public:
  queueContainer( struct tQueueDescriptor* theQueueDescriptor, recoveryLog* theRecoveryLog, bool bRecovery, int theMainFd, const char* objName="queueContainer" );
  virtual ~queueContainer();

  void termChildren( )                              {pWorkers->termChildren();}
  int  respawnChild( int childPid, bool bRespawn )  {int retCode=pWorkers->respawnChild(childPid,bRespawn);feedWorker();return retCode;}
  std::string& getQueueName()                       {return queueName;}
  void dumpHttp( baseEvent* pEvent );
  void resetStats( );
  int  getTotalWorkers( )                           {return pWorkers->getTotalWorkers();}
  void resetItFd( )                                 {pWorkers->resetItFd();}
  int  getNextFd( int& pid, unixSocket*& pSocket )  {return pWorkers->getNextFd(pid,pSocket);}
  void signalChildren( int sig )                    {pWorkers->signalChildren(sig);}
  int  resizeWorkerPool( int newNum )               {return pWorkers->resizeWorkerPool(newNum);}
  void setMaxQueueLen( int m )                      {pQueue->setMaxQueueLen(m);log.info(log.LOGMOSTLY,"setMaxQueueLen:%d",m);}
  void setMaxExecTime( unsigned int m )             {pWorkers->setMaxExecTime(m);}
  void freeze( bool bFreeze );
  void submitEvent( baseEvent* pEvent );
  void feedWorker( );
  void dumpQueue( const char* reason )              {pQueue->dumpQueue(reason);}
  void scanForExpiredEvents( )                      {pQueue->scanForExpiredEvents();}
  void checkOverrunningWorkers( )                   {pWorkers->checkOverrunningWorkers();}
  std::string& getStatusStr( )                      {return statusStr;}
  std::string& getStatus( bool bLog=false );
  std::string& getStatusKey( );

  bool isIdle( )                                    {return pWorkers->isIdle();}
  bool isPersistentApp( )                           {return pWorkers->isPersistentApp();}
  bool isExitWhenDone( )                            {return bExitWhenDone;}
  void releaseWorker( int fd, baseEvent* pEvent );
  void setTime( unsigned int t )                    {now=t;pWorkers->setTime(t);pQueue->setTime(t);}
  unixSocket* getWorkerSock( int workerFd )         {return pWorkers->getWorkerSock(workerFd);}
  void maintenance( );
  void reconfigureCmd( baseEvent* pCommand );
  void sendCommandToChildren( baseEvent* pCommand ) {pWorkers->sendCommandToChildren(pCommand);}
  void exitWhenDone( );
  void shutdown( )                                  {termChildren();freeze(true);bShutdown=true;}
  bool isShutdown( )                                {return bShutdown;}
  void reopenLogfile( );

  private:
  void init( );

  protected:

  // Properties  
  public:

  protected:

  private:
  tQueueDescriptor*                 pContainerDesc;       ///< container descriptor
  baseQueue*                        pQueue;               ///< queue object
  workerPool*                       pWorkers;             ///< worker pool object
  recoveryLog*                      pRecoveryLog;         ///< the recovery log
  std::string                       queueType;            ///< type of queue to be created
  std::string                       queueName;            ///< name by which the queue is known
  std::string                       persistentApp;        ///< persistent app to keep running if not empty
  std::string                       statusStr;            ///< string holding current queue status
  std::string                       statusStrKey;         ///< string holding current queue status key string
  bool                              bRecoveryProcess;     ///< recoveryProcess
  bool                              bWorkersFrozen;       ///< execution frozen
  bool                              bShutdown;            ///< has been shut down
  bool                              bExitWhenDone;        ///< shutdown procedure for persistent apps
  int                               nucleusFd;            ///< nucleus process fd
  unsigned int                      maxExecTime;          ///< max time a worker is allowed to run in seconds, 0 disables
  unsigned int                      now;                  ///< current time
  unsigned int                      maxQueueLength;       ///< max length of the queue
};	// class queueContainer

#endif // !defined( queueContainer_defined_)
