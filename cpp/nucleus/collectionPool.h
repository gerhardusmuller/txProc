/**
 contains a collection of queue/worker pairs
 
 $Id: collectionPool.h 2547 2012-08-30 18:36:42Z gerhardus $
 Versioning: a.b.c a is a major release, b represents changes or new features, c represents bug fixes. 
 @version 1.0.0		21/12/2012		Gerhardus Muller		Script created

 @note

 @todo
 
 @bug

	Copyright Notice
 * **/

#if !defined( collectionPool_defined_ )
#define collectionPool_defined_

#include "nucleus/workerPool.h"
#include "nucleus/baseQueue.h"

class collectionPool : public workerPool
{
  // Definitions
  public:
    static const char *const FROM;

    // Methods
  public:
    collectionPool( tQueueDescriptor* theDescriptor, recoveryLog* theRecoveryLog, bool bRecovery, int theMainFd );
    virtual ~collectionPool();

    virtual void executeEvent( baseEvent* pEvent );
    virtual int anyAvailableWorkers( baseEvent* pEvent );
    virtual int anyAvailableWorkers( int fd );
    virtual bool anyAvailableWorkers( )                                   {return !idleWorkers.empty();}
    virtual void resetIdleIt()                                            {itIdleWorkers=idleWorkers.begin();}
    virtual int getNextIdleFd( int& pid );
    baseQueue* getQueueForPid( int pid );
    baseQueue* getQueueForFd( int fd );

  private:

  protected:
    virtual workerDescriptor* createChild();
    virtual void insertChild( int pid, workerDescriptor* pWorker );
    virtual workerDescriptor* getIdleWorkerByPid( int pid );                                              // gets and deletes it
    virtual unsigned int idleWorkersSize()                                {return idleWorkers.size();}    // idleWorkers is private ..
    virtual void deleteIdleWorkersEntry( int pid )                        {getIdleWorkerByPid(pid);}
    virtual void addIdleWorkersEntry(int pid,workerDescriptor* pWorker)   {idleWorkers.insert(std::pair<int,workerDescriptor*>(pid,pWorker));}

    // Properties  
  public:

  protected:

  private:
    workerMapT                        idleWorkers;            ///< map containing all the idle workers as a (pid,workerDescriptor*) pair
    workerMapIteratorT                itIdleWorkers;          ///< iterator used to access the idle worker fds in sequence
};	// class collectionPool

#endif // !defined( collectionPool_defined_)
