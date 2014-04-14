/**
 $Id: appBase.h 2610 2012-10-03 15:06:49Z gerhardus $
 Versioning: a.b.c a is a major release, b represents changes or new features, c represents bug fixes. 
 @version 1.0.0   17/11/2011    Gerhardus Muller     Script created
 @version 1.1.0   21/08/2012    Gerhardus Muller     support for a startup info command event
 @version 1.2.0		03/10/2012		Gerhardus Muller		 added a startupInfoAvailable and loglevelChanged virtual function callback

 @note

 @todo
 
 @bug

 Copyright Gerhardus Muller
 */

#if !defined( appBase_defined_ )
#define appBase_defined_

#include "utils/object.h"
#include "application/baseEvent.h"
#include <string>

class appBase : public object
{
  // Definitions
  public:

    // Methods
  public:
    appBase( int theArgc, char* theArgv[], const char* theAppName );
    virtual ~appBase( );
    virtual void init();
    virtual void main();
    static void sendSignalCommand( baseEvent::eCommandType command );
    static void sigHandler( int signo );

  protected:
    bool dropPriviledge( const char* user );
    void handleCommand( baseEvent* pCommand );
    virtual void constructDefaultResultEvent( baseEvent* pEvent );  ///< constructs a default resultEvent
    virtual void handleNewEvent( baseEvent* pEvent );               ///< handle a new event
    virtual void handleEvCommand( baseEvent* pEvent );              ///< default handling of EV_COMMAND types
    virtual void handleEvPerl( baseEvent* pEvent );                 ///< default handling of EV_PERL types
    virtual void handleEvUrl( baseEvent* pEvent );                  ///< default handling of EV_URL types
    virtual void handleEvResult( baseEvent* pEvent );               ///< default handling of EV_RESULT types
    virtual void handleEvBase( baseEvent* pEvent );                 ///< default handling of EV_BASE types
    virtual void handleEvError( baseEvent* pEvent );                ///< default handling of EV_ERROR types
    virtual void handleEvOther( baseEvent* pEvent );                ///< default handling for any other EV_ types
    virtual void markAsRejected( const char* result, const char* reason );  ///< unhandled messages are marked as rejected
    virtual void dumpStats( const std::string& time );              ///< default does nothing
    virtual void resetStats();                                      ///< default does nothing
    virtual void handlePersistentCommand( baseEvent* pEvent );      ///< default handling supports common constructs
    virtual void handleUserPersistentCommand( baseEvent* pEvent );  ///< default does nothing
    virtual void handleUnhandledCmdEvents( baseEvent* pEvent );     ///< default does nothing
    virtual void sendDone();
    virtual void handleSignalEvent( baseEvent::eCommandType theCommand ); ///< in process handling of signals
    virtual void appShutdown( baseEvent* pEvent )     {;}           ///< CMD_PERSISTENT_APP, cmd=shutdown 
    virtual void appUnShutdown( baseEvent* pEvent )   {;}           ///< CMD_PERSISTENT_APP, cmd=unshutdown 
    virtual void prepareToExit( baseEvent* pEvent )   {;}           ///< received a CMD_SHUTDOWN, CMD_EXIT_WHEN_DONE or CMD_END_OF_QUEUE
    virtual void startLoopProcess()                   {;}           ///< first statement after the while bRunning
    virtual void setNow( unsigned int tNow )          {;}           ///< callback to set the time at the beginning of the loop - before waiting for an event
    virtual void execMaintenance()                    {;}           ///< hook to implement maintenance tasks off CMD_TIMER_SIGNAL. as soon as one of the shutdown commands have been received this function is called once per second irrespective
    virtual bool canExit()                            {return 1;}   ///< default behaviour is we can exit immediately
    virtual void handleOtherFdEvent( int fd );                      ///< additional file descriptors that can originate events
    virtual void startupInfoAvailable()               {;}           ///< called as soon as a startup info command has been received
    virtual void loglevelChanged( int newLevel )      {;}           ///< called when the loglevel is adjusted

  private:

    // Properties
  public:
    static bool                 bRunning;             ///< true to run, false to exit
    static bool                 bReopenLog;           ///< true if a signal has been received indicating all log to be reopened SIGUSR2
    static bool                 bResetStats;          ///< true to reset stats - SIGUSR1
    static bool                 bDump;                ///< true if a SIGHUP has been received - dump current state and statistics + reset counters
    static bool                 bTimerTick;           ///< true if a timer signal has been received
    static int                  signalFd0;            ///< signalFd[0] for the signal handling

  protected:
    unixSocket*                 pRecSock;             ///< socket for accepting incoming events from stdin
    unixSocket*                 pSignalSock;          ///< socket for accepting signal events
    baseEvent*                  pResultEvent;         ///< obligatory done or result event dispatched by sendDone()
    int                         eventSourceFd;        ///< fd corresponding to pRecSock - normally stdin
    int                         eventReplyFd;         ///< fd for writing replies - normally stdout
    int                         signalFd[2];          ///< unix domain socket to submit signal events to the parent process - the main program listens on [1]
    int                         txProcFd;             ///< unix domain socket to submit new events to txProc
    int                         workerPid;            ///< the worker managing this persistent app's pid - needed amongst others for the collection queue
    std::string                 appName;              ///< name of the application
    std::string                 hostId;               ///< hostname entry
    std::string                 ownQueue;             ///< name of the queue serving this application
    int                         argc;                 ///< command line parameters
    char**                      argv;                 ///< command line parameters
    unsigned int                now;                  ///< time at the beginning of the loop
    unsigned int                termTime;             ///< if > 0 indicates the time when one of the termination commands was received

  private:
};	// class appBase

  
#endif // !defined( appBase_defined_)
