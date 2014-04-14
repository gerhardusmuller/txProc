/**
 Standard queue class
 
 $Id: straightQueue.h 2547 2012-08-30 18:36:42Z gerhardus $
 Versioning: a.b.c a is a major release, b represents changes or new features, c represents bug fixes. 
 @version 1.0.0		10/09/2009		Gerhardus Muller		Script created

 @note

 @todo
 
 @bug

	Copyright Notice
 * **/

#if !defined( straightQueue_defined_ )
#define straightQueue_defined_

#include "nucleus/baseQueue.h"
#include "nucleus/queueContainer.h"

class recoveryLog;
class baseEvent;

class straightQueue : public baseQueue
{
  // Definitions
  public:
    static const char *const FROM;

    // Methods
  public:
    straightQueue( tQueueDescriptor* theDescriptor, recoveryLog* theRecoveryLog, bool bRecovery, const char* objName="straightQueue" );
    virtual ~straightQueue();

    virtual bool canExecuteEventDirectly( baseEvent* pEvent )   {return listSize==0;}
    virtual bool isQueueEmpty( )                                {return !bExitWhenDone && (listSize==0);}
    virtual baseEvent* popAvailableEvent( int fd );
    virtual void queueEvent( baseEvent* pEvent );

    virtual void exitWhenDone( )                                {bExitWhenDone=true;}
    virtual void scanForExpiredEvents( );
    virtual void dumpQueue( const char* reason );
    virtual std::string& getStatus();
    virtual std::string& getStatusKey( );
    

  private:
    void init( );
    baseEvent* popEvent( );
    void checkQueueOverflow( );

  protected:

    // Properties  
  public:

  protected:
    straightQueueT                    eventList;            ///< list of events that can be serviced
    unsigned int                      listSize;             ///< current size of the event list
    bool                              bExitWhenDone;        ///< shutdown procedure for persistent apps

  private:
};	// class straightQueue

#endif // !defined( straightQueue_defined_)
