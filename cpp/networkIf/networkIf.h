/**
 networkIf - provides a networking interface to the outside world
 
 $Id: networkIf.h 2931 2013-10-16 09:47:29Z gerhardus $
 Versioning: a.b.c a is a major release, b represents changes or new features, c represents bug fixes. 
 @version 1.0.0		11/11/2009		Gerhardus Muller		script created

 @note

 @todo
 
 @bug

	Copyright Notice
 */

#if !defined( networkIf_defined_ )
#define networkIf_defined_

#include "utils/object.h"
#include "utils/unixSocket.h"
#include <sys/socket.h> // macos
#include <map>

class recoveryLog;
class baseEvent;
struct tConnectData
{
  unixSocket*   pSocket;
  bool          bFragmentData;                ///< is used infrequently so it makes a little faster than checking for an empty fragmentData
  std::string   fragmentData;
};

class networkIf : public object
{
  // Definitions
  public:
    static const int STALE = 30;              ///< max age in seconds of the clients name for a Unix domain socket
    static const int UN_TYPE;                 ///< Unix domain networkIf type - can be either SOCK_STREAM or SOCK_DGRAM

    // Methods
  public:
    networkIf( int networkIfFd[2], int nucleusFd, int parentFd, recoveryLog* theRecoveryLog, int theArgc, char* theArgv[] );
    virtual ~networkIf();
    virtual std::string toString ();
    void main( );
    static void sigHandler( int signo );

  private:
    void init( int networkIfFd[2], int nucleusFd, int parentFd );
    bool dropPriviledge( const char* user );
    void dumpHttp( const std::string& time );
    bool waitForEvent( );
    int getNextFd( short& eventType );
    int createListenSocket( int type, const std::string& service, std::string& listenAddr );
    int createUnListenSocket( int type, const char* networkIfPath, int qlen=10 );
    int acceptUnSocket( );
    int initServer( int type, const struct sockaddr *addr, socklen_t alen, int qlen=10 );
    void dispatchPacket( int fd, bool bWriteReply=true );
    bool addFdToPoll( int fd, bool bOutAsWell=false );
    void rebuildPollList( bool bAdding=false );
    void closeAndRemoveFd( int fd );
    void sendUdpPacket( baseEvent* pCommand );
    void reconfigure( baseEvent* pCommand );
    void printResultToSocket( unixSocket* pSocket, bool bSuccess, bool bExpectReply=false );
    void writeGreeting( unixSocket* pSocket );

    // Properties
  public:
    static bool                 bRunning;             ///< true to run, false to exit
    static bool                 bReopenLog;           ///< true if a signal has been received indicating all log to be reopened SIGUSR2
    static bool                 bResetStats;          ///< true to reset stats - SIGUSR1
    static bool                 bDump;                ///< true if a SIGHUP has been received - dump current state and statistics + reset counters
    static recoveryLog*         theRecoveryLog;       ///< the recovery log

  protected:

  private:
    std::string                 hostId;               ///< hostname entry
    std::string                 greetingString;       ///< greeting string for external networkIf connections
    std::string                 eventRef;             ///< current event reference
    unixSocket*                 pRecSock;             ///< networkIf for acception incoming events
    int                         eventSourceFd;        ///< fd corresponding to pRecSock
    int                         fdSendSock;           ///< networkIf for sending events to the networkIf process (otherside of pRecSock)
    int                         fdNucleusSock;        ///< networkIf for submitting events to the dispatcher
    int                         fdParentSock;         ///< networkIf for submitting events to the parent mserver
    struct pollfd*              pollFd;               ///< poll fd structure
    int                         maxNumPollFdEntries;  ///< max number of entries in pollFd
    int                         numPollFdEntries;     ///< number of entries in pollFd
    int                         numPollFdsProcessed;  ///< number of entries processed - <= numPollFdsAvailable
    int                         numPollFdsAvailable;  ///< number of fds with available data
    int                         lastFdProcessed;      ///< last fd that was processed
    int                         listenTcpFd;          ///< listen networkIf for tcp connections
    int                         listenUdpFd;          ///< listen networkIf for udp connections
    int                         listenUnFd;           ///< listen networkIf for Unix domain networkIf connections
    std::map<int,tConnectData*> tcpFds;               ///< map containing the tcp networkIfs we are serving
    int                         argc;                 ///< command line parameters
    char**                      argv;                 ///< command line parameters
};	// class networkIf


#endif // !defined( networkIf_defined_)

