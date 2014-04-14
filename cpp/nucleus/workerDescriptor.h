/**
 workerDescriptor - contains a worker
 
 $Id: workerDescriptor.h 2547 2012-08-30 18:36:42Z gerhardus $
 Versioning: a.b.c a is a major release, b represents changes or new features, c represents bug fixes. 
 @version 1.0.0		29/09/2009		Gerhardus Muller		Script created
 @version 1.1.0		23/08/2012		Gerhardus Muller		added a queue member

 @note

 @todo
 
 @bug

	Copyright Notice
 */

#if !defined( workerDescriptor_defined_ )
#define workerDescriptor_defined_

#include "utils/object.h"
#include "utils/unixSocket.h"
#include "nucleus/baseEvent.h"
#include "nucleus/baseQueue.h"
#include "nucleus/queueContainer.h"

class worker;
class recoveryLog;
class queueManagementEvent;

class workerDescriptor : object
{
  // Definitions
  public:

  private:
    static const char *const FROM;
    static const char *const TO_WORKER;

    // Methods
  public:
    workerDescriptor( tQueueDescriptor* theQueueDescriptor, recoveryLog* theRecoveryLog, bool bRecovery, int theMainFd );
    virtual ~workerDescriptor();
    virtual std::string toString ();
    int  forkChild( );
    void shutdownChild( );
    void exitWhenDone( );
    void termChild( );
    void killChild( );
    void ripCarpet( );
    void submitEvent( baseEvent* pEvent, unsigned int now );
    void setPid( int newPid )         {pid = newPid;}
    int  getPid( )                    {return pid;}
    int  getFd( )                     {return fd[0];}
    int  getFd1( )                    {return fd[1];}
    bool isBusy( )                    {return bBusy;}
    void setBusy( bool state )        {bBusy=state;}
    unixSocket* getSock( )            {return pSendSock;}
    bool isTerminal( )                {return bChildInShutdown;}
    bool isKilled( )                  {return bSIGTERM;}
    unsigned int getStartTime( )      {return startTime;}
    void writeRecoveryEntry( );
    void signalChild( int sig );
    void sendCommandToChild( baseEvent::eCommandType command );
    void sendCommandToChild( baseEvent* pCommand );
    void reopenLogfile( )             {log.instanceReopenLogfile();}
    void setQueue( baseQueue* q )     {pQueue=q;}     ///< careful - this is deleted in the destructor
    baseQueue* getQueue()             {return pQueue;}

  private:

    // Properties
  public:
    static recoveryLog*         theRecoveryLog;       ///< the recovery log

  protected:

  private:
    tQueueDescriptor*           pContainerDesc;       ///< container descriptor
    bool                        bChildInShutdown;     ///< flag used to indicate the child has received a shutdown or exit when done command
    bool                        bSIGTERM;             ///< the child has received either a SIGTERM or SIGKILL
    bool                        bBusy;                ///< true if the child is busy
    bool                        bRecoveryProcess;     ///< true for recovery processes
    int                         pid;                  ///< pid
    int                         fd[2];                ///< socketpair used for comms - worker listens on the fd[1] side
    int                         nucleusFd;            ///< nucleus process fd
    unixSocket*                 pSendSock;            ///< socket for sending events to the worker or receiving replies from the worker
    int                         sendFd;               ///< underlying fd for pSendSock
    unsigned int                startTime;            ///< start time of execution
    worker*                     pWorker;              ///< contains the child object
    baseQueue*                  pQueue;               ///< associated queue if relevant - is deleted in the destructor if not NULL
    baseEvent*                  pLastEvent;           ///< kept for recovery purposes
    queueManagementEvent*       pQueueManagement;     ///< class that generates queue management events
    std::string                 recoveryReason;       ///< reason for the recovery event
    std::string                 persistentApp;        ///< persistent app to keep running if not empty
    std::string                 queueName;            ///< queue name
};	// class workerDescriptor

  
#endif // !defined( workerDescriptor_defined_)

