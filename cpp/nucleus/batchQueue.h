/**
 Event queue class supporting round robin scheduling across batches of events
 
 $Id: batchQueue.h 32 2009-09-23 10:14:24Z gerhardus $
 Versioning: a.b.c a is a major release, b represents changes or new features, c represents bug fixes. 
 @version 1.0.0		10/09/2009		Gerhardus Muller		Script created

 @note

 @todo
 
 @bug

	Copyright Notice
 */

#if !defined( batchQueue_defined_ )
#define batchQueue_defined_

#include "dispatcher/baseQueue.h"

// queue of straightQueues
typedef std::deque<straightQueueT*> batchQueueT;
typedef batchQueueT::iterator batchQueueIteratorT;

// map of straightQueues
typedef std::map<unsigned int,straightQueueT*> batchMapT;
typedef batchMapT::iterator  batchMapIteratorT;
typedef std::pair<unsigned int,straightQueueT*> batchMapPairT;

// lookup map to get to the key of a batchQueue based on the object
typedef std::map<straightQueueT*,unsigned int> batchMapLookupT;
typedef batchMapLookupT::iterator  batchMapLookupIteratorT;
typedef std::pair<straightQueueT*,unsigned int> batchMapLookupPairT;

struct tQueueDescriptor;

class batchQueue : public baseQueue
{
  // Definitions
  public:
    static const char *const FROM;
    static const int MAX_SUBQUEUES = 5;
    static const int DEF_NUM_SUBQUEUES = 2;
    static const int DEF_NUM_EVENTS_IN_SEQ_FROM_MAIN_QUEUE = 3;
    static const int DEF_NUM_EVENTS_IN_SEQ_FROM_SUB_QUEUE = 2;

    // Methods
  public:
    batchQueue( tQueueDescriptor* theDescriptor, recoveryLog* theRecoveryLog, bool bRecovery, int theMserverFd, const std::string& thePersistentApp );
    virtual ~batchQueue();
    baseEvent* popAvailableEvent( );

    virtual bool canExecuteEventDirectly( )             {return listSize==0;}
    virtual bool isQueueEmpty( )                        {return listSize==0;}
    virtual void queueEvent( baseEvent* pEvent );
    virtual void scanForExpiredEvents( );
    virtual void dumpList( const char* reason );
    virtual std::string& getStatus( );
    virtual void maintenance( );
    

  protected:

  private:
    void init( );
    baseEvent* popEvent( straightQueueT* theQueue );
    void dropQueue( straightQueueT* pQueue, bool bDropMapLookup, bool bDropMap );
    baseEvent* getEventFromMainQueue( );
    baseEvent* getEventFromSubQueue( );
    straightQueueT* findExistingQueue( int key );
    unsigned int getKeyForQueue( straightQueueT* pQueue, bool bThrowOnNotFound );
    straightQueueT* createBatchQueue( int key );
    void checkQueueOverflow( );
    unsigned int getKeyForQueue( straightQueueT* pQueue, bool bThrowOnNotFound=false );

    // Properties  
  public:

  protected:

  private:
    batchQueueT                       commonQueue;                ///< shared between single and batch events
    batchMapT                         batchMap;                   ///< all batch queues go in here
    batchMapLookupT                   batchMapLookup;             ///< reverse lookup from batchQueue to key
    batchQueueT                       batchOnlyQueue;             ///< queue containing batches with more than one event
    batchQueueT                       subQueues;                  ///< subqueues that contain batches that feed workers - will use it like a ring
    straightQueueT                    mainQueue;                  ///< main queue for single events or queue 0
    unsigned int                      listSize;                   ///< total number of queued events
    int                               numSubQueues;               ///< number of sub queues to round robin over
    int                               maxEventsFromMainQueue;     ///< number of times the main queue is serviced before switching to the sub queues
    int                               maxEventsFromSubQueue;      ///< number of times the sub queues are serviced before switching to the main queue
    int                               numTimesReadFromMainQueue;  ///< number of events we have taken from the main queue in sequence
    int                               numTimesReadFromSubQueue;   ///< number of events we have taken from the sub queue in sequence
    bool                              bFeedingFromMainQueue;      ///< true while feeding off the main queue
};	// class batchQueue

#endif // !defined( batchQueue_defined_)
