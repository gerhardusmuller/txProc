/**
 Base queue class - owns a workerPool and a baseQueue derived object
 
 $Id: baseQueue.h 2547 2012-08-30 18:36:42Z gerhardus $
 Versioning: a.b.c a is a major release, b represents changes or new features, c represents bug fixes. 
 @version 1.0.0		10/09/2009		Gerhardus Muller		Script created

 @note

 @todo
 
 @bug

	Copyright Notice
 * **/

#if !defined( baseQueue_defined_ )
#define baseQueue_defined_

#include "utils/object.h"
#include <map>
#include <deque>

class baseEvent;
class recoveryLog;
struct tQueueDescriptor;
typedef std::deque<baseEvent*> straightQueueT;
typedef straightQueueT::iterator straightQueueIteratorT;

class baseQueue : public object
{
  // Definitions
  public:

    // Methods
  public:
    baseQueue( tQueueDescriptor* theDescriptor, recoveryLog* theRecoveryLog, bool bRecovery, const char* objName="baseQueue" );
    virtual ~baseQueue();

    virtual bool canExecuteEventDirectly( baseEvent* pEvent ) = 0;
    virtual bool isQueueEmpty() = 0;
    virtual baseEvent* popAvailableEvent( int fd ) = 0;
    virtual void queueEvent( baseEvent* pEvent ) = 0;
    virtual void scanForExpiredEvents() = 0;

    virtual std::string& getStatus() = 0;
    virtual std::string& getStatusKey( ) = 0;
    virtual void resetStats();
    virtual void dumpHttp( baseEvent* pEvent );
    virtual baseEvent* checkIfEventIsExpired( baseEvent* pEvent );
    virtual void exitWhenDone( ) = 0;
    virtual void setMaxQueueLen( int m )                    {maxQueueLength=m;}
    virtual void maintenance()                              {;}
    virtual void dumpQueue( const char* reason ) = 0;
    virtual void reopenLogfile( )                           {log.instanceReopenLogfile();}

    void sendResult( baseEvent* pEvent, bool bSuccess, const std::string& result, const std::string& errorString, const std::string& traceTimestamp, const std::string& failureCause, const std::string& systemParam );
    std::string& getQueueName()                             {return queueName;}
    void setTime( unsigned int t )                          {now=t;}

  private:
    void init();

  protected:
    int dumpList( straightQueueT* pQueue, const char* reason );

    // Properties  
  public:

  protected:
    std::string                       queueName;            ///< name by which the queue is known
    std::string                       optionsKey;           ///< key to use for options
    unsigned int                      now;                  ///< current time
    unsigned int                      maxQueueLength;       ///< max length of the queue
    int                               numExpiredEvents;     ///< number of events expired
    bool                              bRecoveryProcess;     ///< recoveryProcess
    std::string                       statusStr;            ///< string holding current queue status
    std::string                       statusStrKey;         ///< string holding current queue status key string
    recoveryLog*                      pRecoveryLog;         ///< the recovery log

  private:

};	// class baseQueue

#endif // !defined( baseQueue_defined_)
