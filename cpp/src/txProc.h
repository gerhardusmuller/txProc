/**
 txProc - main application
 
 $Id: txProc.h 124 2009-11-18 12:57:58Z gerhardus $
 Versioning: a.b.c a is a major release, b represents changes or new features, c represents bug fixes. 
 @version 1.0.0   02/01/2009    Gerhardus Muller    script created

 @note

 @todo
 
 @bug

 Copyright Notice
 */

#if !defined( txProc_defined_ )
#define txProc_defined_

#include "../src/options.h"
#include "utils/unixSocket.h"
#include "nucleus/baseEvent.h"

class nucleus;
class networkIf;
class recoveryLog;

class txProc : public object
{
  // Definitions
  public:

    // Methods
  public:
    txProc( int theArgc, char* theArgv[] );
    virtual ~txProc();
    virtual std::string toString ();
    void testUnixSocket( );
    void testSerialisation( );
    void testnucleus( );
    void main( );
    void recover( );
    static void sigHandler( int signo );
    void rotateLogs( );
    void runAsDaemon( );
    void cleanupObj( );

  private:
    void init( );
    bool handleChildDone( );
    void forkSocket( );
    void forkNucleus( bool bRecovery=false );
    void forkNetworkIf( );
    void waitForChildrenToExit( );
    bool dropPriviledge( const char* user );
    void termChildren( );
    void signalChildren( int sig );
    void dumpHttp( const std::string& time );
    static void sendSignalCommand( baseEvent::eCommandType command );
    void sendCommandToChild( baseEvent::eCommandType command, int fd );
    void sendCommandToChildren( baseEvent::eCommandType command );
    void sendCommandToChildren( baseEvent* pCommand );
    void notifySlaveStatsServers( baseEvent* pCommand );
    void reconfigure( baseEvent* pCommand );

    // Properties
  public:
    static bool                 bRunning;         ///< true to run, false to exit
    static bool                 bChildSignal;     ///< true if a SIGCHLD was caught
    static int                  signalFd0;        ///< signalFd[0] for the signal handling

  protected:

  private:
    unixSocket*             pRecSock;             ///< socket for accepting incoming events
    unixSocket*             pSignalSock;          ///< socket for received signal events
    int                     pid;                  ///< own pid
    int                     eventSourceFd;        ///< fd corresponding to pRecSock
    int                     mainFd[2];            ///< unix domain socket to submit events to the txProc parent process - it reads the [1] side
    int                     networkIfPid;         ///< networkIf process PID
    int                     networkIfFd[2];       ///< unix domain socket to submit events to the networkIf process - networkIf reads the [1] side
    networkIf*              pNetworkIf;           ///< networkIf object
    int                     nucleusPid;           ///< nucleus process PID
    int                     nucleusFd[2];         ///< unix domain socket to submit events to the nucleus - the nucleus listens on [1]
    int                     signalFd[2];          ///< unix domain socket to submit signal events to the txProc parent process - txProc listens on [1] - now using the external socket
    nucleus*                pNucleus;             ///< nucleus object
    recoveryLog*            pRecoveryLog;         ///< recovery log
    std::string             hostId;               ///< hostname entry
    int                     argc;                 ///< command line parameters
    char**                  argv;                 ///< command line parameters
    bool                    bAutoFork;            ///< auto fork child processes if they exit
    unsigned int            now;                  ///< time at the beginning of the loop
};	// class txProc

void cleanup( );
extern const char* buildno;
extern const char* buildtime;
#endif // !defined( txProc_defined_)

