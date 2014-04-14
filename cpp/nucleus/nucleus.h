/**
 nucleus - executes scripts, requests and notifications
 
 $Id: nucleus.h 2890 2013-06-20 15:59:42Z gerhardus $
 Versioning: a.b.c a is a major release, b represents changes or new features, c represents bug fixes. 
 @version 1.0.0		21/09/2009		Gerhardus Muller		Script created
 @version 1.0.0		20/06/2013		Gerhardus Muller		theNetworkIf for the fdsToRemainOpen list

 @note

 @todo
 
 @bug

	Copyright Notice
 */

#if !defined( nucleus_defined_ )
#define nucleus_defined_

#include "utils/object.h"
#include "utils/unixSocket.h"
#include "nucleus/baseEvent.h"
#include "nucleus/optionsNucleus.h"
#include "nucleus/queueContainer.h"
#include <map>
#include <deque>

class workerDescriptor;
class recoveryLog;
class queueContainer;
class network;

typedef std::map<std::string,queueContainer*> queueContainerStrMapT;
typedef queueContainerStrMapT::iterator queueContainerStrMapIteratorT;
typedef std::pair<std::string,queueContainer*> queueContainerStrPairT;

typedef std::map<int,queueContainer*> queueContainerIntMapT;
typedef queueContainerIntMapT::iterator queueContainerIntMapIteratorT;
typedef std::pair<int,queueContainer*> queueContainerIntPairT;

class nucleus : public object
{
  // Definitions
  public:
    static const char *const FROM;
    static const char *const TO_WORKER;

    // Methods
  public:
    nucleus( int nucleusFd[2], int theParentFd, recoveryLog* theRecoveryLog, bool bRecovery, int theArgc, char* theArgv[], int theNetworkIf );
    virtual ~nucleus();
    virtual std::string toString ();
    void init( );
    void main( );
    static void sigHandler( int signo );

  private:
    void createQueues( );
    bool createQueue( const std::string& q );
    void dropQueue( const std::string& q );
    void respawnChild( );
    void queueEvent( baseEvent* pEvent );
    queueContainer* routeNonLocalqueue( const std::string& destQueue );
    bool dropPriviledge( const char* user );
    void dumpLists( const char* reason );
    void signalChildren( int sig );
    void dumpHttp( const std::string& time );
    void sendResult( baseEvent* pEvent, bool bSuccess, const std::string& result, const std::string& errorString=std::string(), const std::string& traceTimestamp=std::string(), const std::string& failureCause=std::string(), const std::string& systemParam=std::string() );
    void scanForExpiredEvents( );
    void checkOverrunningWorkers( );
    void sendCommandToChildren( baseEvent::eCommandType command );
    void sendCommandToChildren( baseEvent* pCommand );
    void exitWhenDone( );
    void updateStats( baseEvent* pDone );
    void waitForChildrenToExit( );
    void reconfigure( baseEvent* pCommand );
    void workerReconfigure( baseEvent* pCommand );
    void buildLookupMaps( );
    queueContainer* findQueueByName( const std::string& queueName, bool bThrow=true );
    void writeStats( const std::string& time );
    void closeStatsFiles( );
    void openStatsFiles( int startI, int num );
    void createStatsDir( int i );
    static void sendSignalCommand( baseEvent::eCommandType command );

    // Properties
  public:
    static bool                 bRunning;                         ///< true to run, false to exit
    static bool                 bReopenLog;                       ///< true if a signal has been received indicating all logs to be reopened
    static bool                 bResetStats;                      ///< true if a signal has been received indicating the recovery log to be reopened
    static bool                 bDump;                            ///< true if a SIGHUP has been received - dump current state and statistics + reset counters
    static bool                 bTimerTick;                       ///< true if a timer event has occurred
    static int                  sigTermCount;                     ///< counts the number of times we have been asked to shutdown
    static recoveryLog*         theRecoveryLog;                   ///< the recovery log
    static int                  signalFd0;                        ///< signalFd[0] for the signal handling

  protected:

  private:
    queueContainerStrMapT             queues;                     ///< map containing all the queues
    queueContainerIntMapT             workerFds;                  ///< map containing all the file descriptors of the workers
    queueContainerIntMapT             workerPids;                 ///< map containing all the pids of the workers
    tQueueDescriptor*                 queueDesc;                  ///< carries the queue descriptors
    unsigned int                      numQueues;                  ///< number of entries in the queueDesc array
    int                               totNumWorkers;              ///< number of workers across all the queues
    network*                          pNetwork;                   ///< network object
    unixSocket*                       pRecSock;                   ///< socket for accepting incoming events
    unixSocket*                       pSignalSock;                ///< socket for received signal events
    int                               eventSourceFd;              ///< fd corresponding to pRecSock
    int                               eventSourceWriteFd;         ///< write side of the eventSourceFd socket - ie the fd for other processes to submit events to the nucleus for processing
    int                               networkIfFd;                ///< write side of the networkIF process socket
    int                               parentFd;                   ///< parent fd
    int                               numRecoveryEvents;          ///< number of recovery events in the recovery log for the time period
    int                               signalFd[2];                ///< unix domain socket to submit signal events to the process - parent listens on [1]
    bool                              bRecoveryProcess;           ///< recoveryProcess
    bool                              bExitOnDone;                ///< exit as soon as the last worker has finished
    unsigned int                      nextExpiredEventCheck;      ///< next time to check for expired event
    unsigned int                      now;                        ///< time at the beginning of the loop
    std::string                       hostId;                     ///< hostname entry
    int                               argc;                       ///< command line parameters
    char**                            argv;                       ///< command line parameters
};	// class nucleus

#endif // !defined( nucleus_defined_)

