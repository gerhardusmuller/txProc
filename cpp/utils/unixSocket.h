/**
 unixSocket - implementation of Unix domain sockets
 
 $Id: unixSocket.h 2881 2013-06-09 16:28:13Z gerhardus $
 Versioning: a.b.c a is a major release, b represents changes or new features, c represents bug fixes. 
 @version 1.0.0		28/09/2009		Gerhardus Muller		Script created
 @version 1.1.0		30/03/2011		Gerhardus Muller		added a label to createSocketPair
 @version 1.2.0		20/03/2012		Gerhardus Muller		changed READ_BUF_SIZE to 32768
 @version 1.3.0		05/09/2012		Gerhardus Muller		added an event type
 @version 1.4.0		27/02/2013		Gerhardus Muller		added getPipe
 @version 1.5.0		05/06/2013		Gerhardus Muller		added the FD_CLOEXEC flag to the socketpair call; added setCloseOnExec

 @note

 @todo
 
 @bug

	Copyright Notice
 */

#if !defined( unixSocket_defined_ )
#define unixSocket_defined_

#include "utils/object.h"
#include <sys/poll.h>

class unixSocket : public object
{
  // Definitions
  public:
  enum eEventType { ET_OTHER,ET_QUEUE_EVENT,ET_WORKER_RET,ET_SIGNAL,ET_LISTEN,ET_WORKER_PIPE };
  static const int READ_BUF_SIZE = 32768;
  //  static const int READ_BUF_SIZE = 4096;

  // Methods
  public:
  unixSocket( int fd, eEventType eType=ET_OTHER, bool isudp=false, const char* name = "unixSocket" );
  virtual ~unixSocket();
  virtual std::string toString ();

  static void createSocketPair( int socketfd[2], const char* label=NULL, bool bCloseOnExec=true );
  static void setCloseOnExec( bool bSet, int fd );
  std::string read( );
  std::streamsize read(char* s, std::streamsize n);
  std::streamsize write(const char* s, std::streamsize n);
  static std::streamsize writeOnce( int fd, const std::string& s, bool bPipe=false );
  static std::streamsize writeOnceTo( int fd, const std::string& s, const std::string& dest );
  void setCloseOnExec( bool bSet );
  void setNonblocking( );
  void setNoSigPipe( );
  void returnUnusedCharacters( const char* buf, int n );
  int  getSocketFd( )                             {return socketfd;};
  bool isEof( )                                   {return bEof;};
  void resetEof( )                                {bEof=false;};
  void setUdp( )                                  {bUdp=true;};
  int  getPollTimeout(  )                         {return pollTimeout;};
  void setPollTimeout( int t )                    {pollTimeout=t;}; // timeout is in ms
  void setLogLevel( int l )                       {log.setNewLevel((loggerDefs::eLogLevel)l);}; // higher level logs more
  eEventType getEventType()                       {return eventType;}

  //  bool waitForEvent( const sigset_t *sigmask=NULL );
  bool waitForEvent( );
  bool multiFdWaitForEvent( );
  void initPoll( int numFds );
  void resetPoll( )                               {pollFdCount=0;}
  int  getNextFd( );
  int  getLastErrorFd( )                          {return lastErrorFd;}
  void addReadFd( int fd );
  void setPipe( )                                 {bPipe=true;}
  bool getPipe( )                                 {return bPipe;}
  void setThrowEof( )                             {bThrowEof=true;}
  static const char* eEventTypeToStr( eEventType e );

  private:

  // Properties
  public:
  static logger*              pStaticLogger;              ///< class scope logger
  static logger               staticLogger;               ///< class scope logger

  protected:

  private:
  eEventType                        eventType;            ///< indicates the type of events to be expected on this socket
  int                               socketfd;             ///< socket pair file descriptors
  bool                              bEof;                 ///< set to true on eof (socket closed) condition
  bool                              bUdp;                 ///< true for a udp type socket where the entire message must be read in one go
  bool                              bPipe;                ///< true if we are reading from a pipe
  bool                              bThrowEof;            ///< if true throw on read on eof
  struct pollfd*                    pollFd;               ///< poll fd structure
  int                               numPollFdEntries;     ///< number of entries in pollFd
  int                               numPollFdsProcessed;  ///< number of entries processed - <= numPollFdsAvailable
  int                               numPollFdsAvailable;  ///< number of fds with available data
  int                               lastFdProcessed;      ///< last fd that was processed
  int                               lastErrorFd;          ///< last fd with a detected error on it
  int                               pollFdCount;          ///< used to assemble the pollFd structure
  int                               pollTimeout;          ///< time that poll blocks in milliseconds
  std::string                       unusedChars;          ///< characters read from the socket and left over
};	// class unixSocket

#endif // !defined( unixSocket_defined_)

