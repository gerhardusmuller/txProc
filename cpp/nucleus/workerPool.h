/**
 worker pool class
 
 $Id: workerPool.h 2558 2012-09-06 17:48:26Z gerhardus $
 Versioning: a.b.c a is a major release, b represents changes or new features, c represents bug fixes. 
 @version 1.0.0		10/09/2008		Gerhardus Muller		Script created
 @version 2.0.0		16/08/2012		Gerhardus Muller		support for individually addressable workers

 @note

 @todo
 
 @bug

	Copyright Notice
 * **/

#if !defined( workerPool_defined_ )
#define workerPool_defined_

#include "utils/object.h"

#include <map>
#include <deque>

class workerDescriptor;
class baseEvent;
class unixSocket;
class recoveryLog;
class queueManagementEvent;
struct tQueueDescriptor;  // defined in queueContainer.h

typedef std::deque<int> idleWorkersT;
typedef idleWorkersT::iterator idleWorkersIteratorT;
typedef std::map<int,workerDescriptor*> workerMapT;
typedef workerMapT::iterator workerMapIteratorT;

class workerPool : public object
{
  // Definitions
  public:

    // Methods
  public:
    workerPool(  struct tQueueDescriptor* theQueueDescriptor, recoveryLog* theRecoveryLog, bool bRecovery, int theMainFd, const char* name="workerPool" );
    virtual ~workerPool();

    virtual int anyAvailableWorkers( baseEvent* pEvent )                {return idleWorkers.empty()?-1:0;}    ///< has a different implementation in collectionPool
    virtual int anyAvailableWorkers( int fd )                           {return idleWorkers.empty()?-1:fd;}
    virtual bool anyAvailableWorkers( )                                 {return !idleWorkers.empty();}
    virtual void executeEvent( baseEvent* pEvent );
    void resetItFd()                                                    {itFd=workerFds.begin();}
    int getNextFd( int& pid, unixSocket*& pSocket );
    virtual void resetIdleIt()                                          {;}                                   ///< not used for the normal workerPool
    virtual int getNextIdleFd( int& pid );
    void setMaxExecTime( unsigned int m );
    unixSocket* getWorkerSock( int workerFd );
    void setTime( unsigned int t )                                      {now=t;}

    virtual int  resizeWorkerPool( int newNum );
    virtual int  respawnChild( int childPid, bool bRespawn );
    virtual void reconfigure( baseEvent* pCommand );
    virtual int  countIdle( bool bCountLong );
    virtual void releaseWorker( int fd, baseEvent* pEvent );
    virtual void termChildren( );
    virtual bool isIdle( );
    virtual void checkOverrunningWorkers( );
    virtual std::string& getStatus();
    virtual std::string& getStatusKey( );
    virtual void resetStats( );
    void signalChildren( int sig );
    void sendCommandToChildren( baseEvent* pCommand );
    void exitWhenDone( );
    bool isPersistentApp( )                                             {return bPersistentApp;}
    int  getTotalWorkers( )                                             {return totalWorkers;}
    void reopenLogfile( );

  private:

  protected:
    virtual void init( );
    virtual workerDescriptor* createChild( );
    virtual workerDescriptor* removeChild( int pid );
    virtual void insertChild( int pid, workerDescriptor* pWorker );
    virtual workerDescriptor* getIdleWorkerByPid( int pid )             {if(idleWorkers.empty())throw Exception(log,log.WARN,"getIdleWorkerByPid:idleWorkers empty pid:%d",pid);pid=idleWorkers.back();idleWorkers.pop_back();return workers[pid];}   ///< return the next idle worker in round-robin fashion
    virtual unsigned int idleWorkersSize()                              {return idleWorkers.size();}
    virtual void deleteIdleWorkersEntry( int pid );
    virtual void addIdleWorkersEntry(int pid,workerDescriptor* pWorker) {idleWorkers.push_front(pid);}
    virtual void updateStats( baseEvent* pDone );
    workerDescriptor* getWorkerForPid( int pid );
    workerDescriptor* getWorkerForFd( int fd );

    // Properties  
  public:

  protected:
    tQueueDescriptor*                 pContainerDesc;       ///< container descriptor
    std::string                       queueName;            ///< name by which the queue is known
    recoveryLog*                      pRecoveryLog;         ///< the recovery log
    workerMapT                        workers;              ///< map containing all the workers as a (pid,workerDescriptor*) pair
    workerMapT                        workerFds;            ///< map containing all the workers as a (fd,workerDescriptor*) pair
    workerMapIteratorT                itFd;                 ///< iterator used to access the fds in sequence
    queueManagementEvent*             pQueueManagement;     ///< class that generates queue management events
    unsigned int                      now;                  ///< current time
    unsigned int                      execTimeLimit;        ///< max time a worker is allowed to run in seconds, 0 disables
    unsigned int                      accExecTime;          ///< accumulated execution time
    unsigned int                      countExecEvents;      ///< number of events counted
    unsigned int                      maxExecTime;          ///< max time a worker was executing
    unsigned int                      numRecoveryEvents;    ///< as the name suggests
    unsigned int                      accQueueTime;         ///< accumulative time in queue
    unsigned int                      maxQueueTime;         ///< max queue time
    unsigned int                      countQueueEvents;     ///< number of queued events
    bool                              bRecoveryProcess;     ///< recoveryProcess
    int                               nucleusFd;            ///< nucleus process fd

  private:
    std::string                       statusStr;            ///< string holding current queue status
    std::string                       statusStrKey;         ///< string holding current queue status key string
    idleWorkersT                      idleWorkers;          ///< workers not engaged in any task - a deque of pids
    bool                              bPersistentApp;       ///< true if we are in persistent mode
    bool                              bExitWhenDone;        ///< received a CMD_EXIT_WHEN_DONE - only useful in the context of a persistent app
    int                               totalWorkers;         ///< number of workers available to service the queue
};	// class workerPool

#endif // !defined( workerPool_defined_)
