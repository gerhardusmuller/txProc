/**
 worker - actual worker that can execute scripts, http get requests and later send mail
 
 $Id: worker.h 2880 2013-06-06 15:39:03Z gerhardus $
 Versioning: a.b.c a is a major release, b represents changes or new features, c represents bug fixes. 
 @version 1.0.0		29/09/2009		Gerhardus Muller		Script created

 @note

 @todo
 
 @bug

	Copyright Notice
 */

#if !defined( worker_defined_ )
#define worker_defined_

#include "utils/object.h"
#include "utils/unixSocket.h"
#include "nucleus/optionsNucleus.h"
#include "nucleus/baseEvent.h"
#include "nucleus/queueContainer.h"

class urlRequest;
class scriptExec;
class recoveryLog;
class queueManagementEvent;

class worker : public object
{
  // Definitions
  public:
    static const int URL_MAX_TIME_OFFSET = 5;  ///< put the curl lib offset at 10 seconds less than the process max time

  private:
    static const char *const FROM;
    static const char *const TO_WORKER_DESCRIPTOR;

    // Methods
  public:
    worker( );
    worker( tQueueDescriptor* theQueueDescriptor, int theFd, recoveryLog* theRecoveryLog, bool bRecovery, int theMainFd );
    virtual ~worker();
    virtual std::string toString ();
    static void sigHandler( int signo );
    void main( );
    int getFd( )                      {return fd;}
    int getPid( )                     {return pid;}

  private:
    void logForRecovery( baseEvent* pEvent, bool bSuccess, const std::string& error );
    void process( baseEvent* pEvent );
    void sendResult( baseEvent* pEvent, bool bSuccess, const std::string& result, const std::string& errorString=std::string(), const std::string& traceTimestamp=std::string(), const std::string& failureCause=std::string(), const std::string& systemParam=std::string(), baseEvent* pResult=NULL );
    void sendDone( );
    void closeOpenFileHandles( );
    void dumpHttp( const std::string& time );
    void reconfigure( baseEvent* pCommand );
    static void sendSignalCommand( baseEvent::eCommandType command );

    // Properties
  public:
    static bool                 bRunning;             ///< true to run, false to exit
    static bool                 bReopenLog;           ///< true if a signal has been received indicating all logs to be reopened
    static bool                 bResetStats;          ///< true if a signal has been received indicating the recovery log to be reopened
    static bool                 bDump;                ///< true if a SIGHUP has been received - dump current state and statistics + reset counters
    static scriptExec*          pScriptExec;          ///< object used to exec scripts
    static recoveryLog*         theRecoveryLog;       ///< the recovery log
    static int                  pid;                  ///< worker pid
    static int                  signalFd0;            ///< signalFd[0] for the signal handling

  protected:

  private:
    tQueueDescriptor*           pContainerDesc;       ///< container descriptor
    bool                        bRecoveryProcess;     ///< recoveryProcess
    bool                        bWroteRecovery;       ///< true if a recovery event was generated
    bool                        bPersistentApp;       ///< true if we are in persistent mode
    bool                        bExitWhenDone;        ///< received an exit when done command - only useful if running a persistent app
    int                         signalFd[2];          ///< unix domain socket to submit signal events to the parent process - parent listens on [1]
    int                         fd;                   ///< file descriptor of unix domain socket on which the worker should listen for instructions
    int                         nucleusFd;            ///< nucleus process file descriptor
    int                         timeStarted;          ///< time at which the process was started in seconds
    int                         elapsedTime;          ///< running time of the process in seconds
    int                         maxTimeToRun;         ///< max time for curl library
    unsigned int                numHits;              ///< number of events processed by this worker
    baseEvent                   persistentScript;     ///< persistent script object if so defined
    unixSocket*                 pRecSock;             ///< socket for receiving events
    unixSocket*                 pSignalSock;          ///< socket for received signal events
    urlRequest*                 pUrlRequest;          ///< object used for URL requests / notifications
    queueManagementEvent*       pQueueManagement;     ///< class that generates queue management events
    std::string                 queueName;            ///< queue name
    std::string                 persistentApp;        ///< persistent app to keep running if not empty - after initial parsing it is for logging only
    std::string                 persistentParams;     ///< persistent script parameters for logging only
    std::string                 defaultScript;        ///< if no script is specified for EV_PERL/EV_SCRIPT/EV_BIN events
    std::string                 defaultUrl;           ///< if no url is specified for EV_URL events
};	// class worker

  
#endif // !defined( worker_defined_)

