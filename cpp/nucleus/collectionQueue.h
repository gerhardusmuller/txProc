/**
 Collection queue class
 
 $Id: collectionQueue.h 2555 2012-09-04 14:12:00Z gerhardus $
 Versioning: a.b.c a is a major release, b represents changes or new features, c represents bug fixes. 
 @version 1.0.0		17/08/2012		Gerhardus Muller		Script created

 @note

 @todo
 
 @bug

	Copyright Notice
 * **/

#if !defined( collectionQueue_defined_ )
#define collectionQueue_defined_

#include "nucleus/baseQueue.h"

class recoveryLog;
class baseEvent;
class collectionPool;
struct tQueueDescriptor;

class collectionQueue : public baseQueue
{
  // Definitions
  public:

    // Methods
  public:
    collectionQueue( tQueueDescriptor* theDescriptor, recoveryLog* theRecoveryLog, bool bRecovery, collectionPool* theWorkers );
    virtual ~collectionQueue();

    virtual bool canExecuteEventDirectly( baseEvent* pEvent );
    virtual bool isQueueEmpty()                                   {return bExitWhenDone;}     ///< this is only a front and only used in feedWorker
    virtual baseEvent* popAvailableEvent( int fd );
    virtual void queueEvent( baseEvent* pEvent );

    virtual void exitWhenDone()                                   {bExitWhenDone=true;}
    virtual void scanForExpiredEvents();
    virtual void dumpQueue( const char* reason );
    virtual std::string& getStatus();
    virtual std::string& getStatusKey();

  private:
    void init();

  protected:

    // Properties  
  public:

  protected:
    collectionPool*                   pWorkers;             ///< worker pool object for which this is a front
    bool                              bExitWhenDone;        ///< shutdown procedure for persistent apps

  private:
};	// class collectionQueue

#endif // !defined( collectionQueue_defined_)
