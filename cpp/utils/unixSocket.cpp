/** @class unixSocket
 unixSocket - implementation of Unix domain sockets
 
 $Id: unixSocket.cpp 2880 2013-06-06 15:39:03Z gerhardus $
 Versioning: a.b.c a is a major release, b represents changes or new features, c represents bug fixes. 
 @version 1.0.0		28/09/2009		Gerhardus Muller		Script created
 @version 1.1.0		02/11/2010		Gerhardus Muller		unused characters should be pushed in front of existing characters in the buffer
 @version 1.2.0		09/02/2011		Gerhardus Muller		multiFdWaitForEvent only to return true when there is at least one file descriptor ready / added setLogLevel
 @version 1.3.0		30/03/2011		Gerhardus Muller		added a label to createSocketPair
 @version 1.4.0		22/05/2011		Gerhardus Muller		support for SO_NOSIGPIPE on mac
 @version 1.5.0		07/05/2013		Gerhardus Muller		added displaying the system error on socketpair failing
 @version 1.6.0		04/06/2013		Gerhardus Muller		changed the loglevel in multiFdWaitForEvent
 @version 1.7.0		05/06/2013		Gerhardus Muller		added the FD_CLOEXEC flag to the socketpair call; added setCloseOnExec
 @version 1.8.0		06/08/2014		Gerhardus Muller		added writeOnceTo

 @note

 @todo
 
 @bug

	Copyright Notice
 */

#include "utils/unixSocket.h"
#include <errno.h>
#include <string.h>
#include <sys/types.h>
#include <sys/socket.h> 
#include <sys/un.h>
#include <signal.h>
#include <unistd.h>
#include <fcntl.h>

logger unixSocket::staticLogger = logger( "unixSocketS", loggerDefs::MIDLEVEL );
logger* unixSocket::pStaticLogger = &unixSocket::staticLogger;

/**
 Construction
 */
unixSocket::unixSocket( int fd, eEventType eType, bool isUdp, const char* name )
  : object( name )
{
  socketfd = fd;
  eventType = eType;
  bEof = false;
  bThrowEof = false;
  bUdp = isUdp;
  pollFd = NULL;
  numPollFdEntries = 0;
  pollTimeout = -1;
  bPipe = false;
  lastErrorFd = -1;
}	// unixSocket

/**
 Destruction
 */
unixSocket::~unixSocket()
{
  log.debug( log.LOGNORMAL, "unixSocket::~unixSocket pollFd %p", pollFd );
  if( pollFd != NULL )
  {
    delete[] pollFd;
    pollFd = NULL;
  }
}	// ~unixSocket

/**
 Standard logging call - produces a generic text version of the unixSocket.
 Memory allocation / deleting is handled by this unixSocket.
 @return pointer to a string describing the state of the unixSocket.  The string has an unspecified lifetime and is not multi-thread safe
 */
std::string unixSocket::toString( )
{
  std::ostringstream oss;
  oss << typeid(*this).name() << ":" << this << " fd " << socketfd;
	return oss.str();
}	// toString

/**
 * Create a socketpair
 * @param socketfd - array in which to store the socketpair
 * @param label - optional logging label
 * @param bCloseOnExec - default true - closes the fd over an exec call
 * @exception on failure to create
 * **/
void unixSocket::createSocketPair( int socketfd[2], const char* label, bool bCloseOnExec )
{
  /* allocate socketpair resource */ 
  int flags = SOCK_STREAM;
  if( bCloseOnExec ) flags |= SOCK_CLOEXEC;
  int retVal = socketpair( AF_LOCAL, flags, 0, socketfd );
  if ( -1 == retVal )
    throw Exception( *pStaticLogger, loggerDefs::ERROR, "failed to create socketpair - %s", strerror( errno ) );

  if( label == NULL ) label = "";
  pStaticLogger->debug( loggerDefs::LOGMOSTLY, "createSocketPair %d,%d - %s bCloseOnExec:%d", socketfd[0], socketfd[1], label, bCloseOnExec );
} // createSocketPair

/**
 * waits in blocking mode for a message / event
 * does not restart after a signal
 * NOTE: do not use this function with a sigmask on kernels that do not support the
 * ppoll function.  rather use a loopback pipe that the signals insert a message into
 * that is waited on in the main loop using multiFdWaitForEvent
 * @param sigmask is an optional mask to signals while waiting - can be set to NULL
 * @return true for an input event ready
 * @exception on error
 * **/
//bool unixSocket::waitForEvent( const sigset_t *sigmask )
bool unixSocket::waitForEvent(  )
{
  struct pollfd pfd[2];
//  const sigset_t *sigmask = NULL;

  /* wait (in blocking mode) for any events */
  pfd[0].fd     = socketfd; 
  pfd[0].events = POLLIN; 
  int retVal = 0;
//  if( sigmask != NULL )
//  {
//#ifdef PPOLL_NOT_AVAILABLE
//    // for Debian or older kernels without ppoll support
//    // this enables substitute functionality - to be used with care as it could cause a 
//    // race condition in mserver whereby it cannot see signals
//    // something like the stats UDP command should take it out again
//    sigset_t origmask;
//    sigprocmask( SIG_SETMASK, sigmask, &origmask );
//    retVal = poll( pfd, 1, pollTimeout );
//    sigprocmask( SIG_SETMASK, &origmask, NULL );
//#else
//    struct timespec timeoutStruct;
//    struct timespec* pTimeout = NULL;
//    if( pollTimeout != -1 )
//    {
//      pTimeout = &timeoutStruct;
//      timeoutStruct.tv_sec = pollTimeout / 1000;
//      long remainderMs = pollTimeout - timeoutStruct.tv_sec*1000;
//      timeoutStruct.tv_nsec = remainderMs * 1000000;
//    } // if
//    retVal = ppoll( pfd, 1, pTimeout, sigmask );
//#endif
//  } // if
//  else
//  {
    retVal = poll( pfd, 1, pollTimeout );
//  }

  log.generateTimestamp();
  if( retVal == -1 )
  {
    if( errno == EINTR ) return false;  // we have been interrupted but no new messages
    throw Exception( log, log.DEBUG, "waitForEvent: returned -1 error %s", strerror( errno ) );
  } // if
  if( retVal == 0 ) return false;       // timeout

  /* There was an event, call handler */
  if( pfd[0].revents &(POLLIN | POLLPRI) )
    return true;
  else if( pfd[0].revents & POLLERR )
    throw Exception( log, log.DEBUG, "waitForEvent: returned POLLERR" );
  else if( pfd[0].revents & POLLHUP )
    throw Exception( log, log.DEBUG, "waitForEvent: returned POLLHUP" );
  else if( pfd[0].revents & POLLNVAL )
    throw Exception( log, log.DEBUG, "waitForEvent: returned POLLNVAL" );
  else
    throw Exception( log, log.DEBUG,  "waitForEvent: unspecified error" );
} // waitForEvent

/** 
 * stream interface - reading from socket
 * restart the recv if we were interrupted by a signal
 * @param s - is the output buffer
 * @param n - is the size of the output buffer
 * @return -1 on nothing available or eof or other error - bEof will be set if end of file condition was set
 * @exception throws on eof - set by calling setThrowEof()
 * **/
std::streamsize unixSocket::read( char* s, std::streamsize n )
{
  using namespace std;
  int bytesReceived = 0;
  char* buf = s;
  if( log.wouldLog( log.LOGSELDOM ) )
    log.debug( log.MIDLEVEL, "read requested for %d bytes", n );

  // for Udp we always pre-read into the unusedChars buffer if the buffer is empty
  if( bUdp && unusedChars.empty() )
  {
    char tempBuf[READ_BUF_SIZE];
    do 
    {
      if( bPipe )
        bytesReceived = ::read( socketfd, tempBuf, READ_BUF_SIZE-1 );
      else
        bytesReceived = recv( socketfd, tempBuf, READ_BUF_SIZE-1, 0 );
    }
    while( ( bytesReceived == -1 ) && ( errno == EINTR ) );
    if( bytesReceived == 0 )
    {
      bEof = true;
      if( bThrowEof ) throw Exception( log, log.ERROR, "read: eof on fd:%d", socketfd );
      return -1;
    } // if
    else if( bytesReceived == -1 )
    {
      // given that this is the result of a poll return we should not normally get a EAGAIN
      if( errno != EAGAIN )
        log.warn( log.MIDLEVEL, "bUdp read error on fd %d - %s", socketfd, strerror( errno ) );
      return -1;    // eof or other error
    } // else if
    else
    {
      tempBuf[bytesReceived] = '\0';
      if( log.LOGSELDOM <= log.getLogLevel() ) log.debug( log.LOGONOCCASION ) << "bUdp read len " << bytesReceived << " bytes: " << tempBuf;
      else if( log.LOGONOCCASION < log.getLogLevel() ) log.debug( log.MIDLEVEL, "bUdp read len %d bytes", bytesReceived );
      unusedChars.append( tempBuf, bytesReceived );
    } // else( ( bytesReceived
    bytesReceived = 0;
  } // if( bUdp

  // output any unused characters first
  int lenAvailable = unusedChars.length();
  int lenToCopy = 0;
  if( lenAvailable > 0 )
  {
    lenToCopy = (lenAvailable>n)?n:lenAvailable;
    memcpy( buf, unusedChars.data(), lenToCopy );
    unusedChars.erase( 0, lenToCopy );
    n -= lenToCopy;
    buf += lenToCopy;
    *buf = '\0';
  } // unusedChars

  // read the balance of the characters from the input socket if any more are required
  if( n > 0 )
  {
    do 
    {
      if( bPipe )
        bytesReceived = ::read( socketfd, buf, n );
      else
        bytesReceived = recv( socketfd, buf, n, 0 );
    }
    while( ( bytesReceived == -1 ) && ( errno == EINTR ) );
    if( ( bytesReceived == 0 ) && ( n != 0 ) )
    {
      bEof = true;
      if( bThrowEof ) throw Exception( log, log.ERROR, "read: eof on fd:%d", socketfd );
      return -1;
    }
    else if( bytesReceived == -1 )
    {
      // given that this is the result of a poll return we should not normally get a EAGAIN
      if( errno != EAGAIN )
        log.warn( log.LOGALWAYS, "read error on fd:%d - %s", socketfd, strerror(errno) );
      else if( lenToCopy > 0 )
        returnUnusedCharacters( s, lenToCopy );
        
      log.debug( log.LOGSELDOM, "read: no data or other error" );
      return -1;    // eof or other error
    } // else if
    else
    {
      s[bytesReceived+lenToCopy] = '\0';
      if( log.wouldLog( log.LOGSELDOM ) ) log.debug( log.LOGONOCCASION ) << "fd:" << socketfd << " read1 len:" << (bytesReceived+lenToCopy) << " bytes:'" << s << "'";
      else if( log.wouldLog( log.LEVEL8 ) ) log.debug( log.MIDLEVEL, "fd:%d read1 buf:%d len:%d bytes", socketfd, n, bytesReceived+lenToCopy );
      return bytesReceived + lenToCopy;
    } // else( ( bytesReceived
  } // if( n > 0
  
//(log.debug( log.LOGNORMAL ) << "packet data: ").hexDump( std::string((const char*)s,bytesReceived+lenToCopy) );
  if( log.wouldLog( log.LOGSELDOM ) ) log.debug( log.LOGONOCCASION ) << "fd:" << socketfd << " read len:" << (bytesReceived+lenToCopy) << " bytes:'" << s << "'";
  else if( log.wouldLog( log.LEVEL8 ) ) log.debug( log.MIDLEVEL, "fd:%d read len:%d bytes", socketfd, bytesReceived+lenToCopy );
  return bytesReceived + lenToCopy;
} // read

/**
 * read the contents of the socket
 * **/
std::string unixSocket::read( )
{
  char strBuf[READ_BUF_SIZE];
  int bufLen = READ_BUF_SIZE;
  int lenAvailable = unusedChars.length();
  int lenToCopy = 0;
  if( lenAvailable > 0 )
  {
    lenToCopy = (lenAvailable>READ_BUF_SIZE)?READ_BUF_SIZE:lenAvailable;
    memcpy( strBuf, unusedChars.data(), lenToCopy );
    unusedChars.erase( 0, lenToCopy );
    bufLen -= lenToCopy;
  } // if
  
  int newLen = read( strBuf+lenToCopy, bufLen );
  return std::string( strBuf, newLen+lenToCopy );
} // read

/**
 * returns unused characters
 * ununsed characters should be pushed in front of existing characters
 * @param buf - buffer containing the characters
 * @param n - number of characters to return
 * **/
void unixSocket::returnUnusedCharacters( const char* buf, int n )
{
  if( n > 0 )
  {
    if( unusedChars.empty() )
      unusedChars.append( buf, n );
    else
    {
      std::string tmp = unusedChars;
      unusedChars.assign( buf, n );
      unusedChars.append( tmp );
    } // else
    log.debug( log.MIDLEVEL, "returnUnusedCharacters returned %d characters fd:%d", n, socketfd );
  }
  else
    log.debug( log.MIDLEVEL, "returnUnusedCharacters returned 0 characters fd:%d", socketfd );
} // returnUnreadCharacters
  
/**
 * TODO need to send in a loop and check for EAGAIN or EWOULDBLOCK to ensure all data is delivered
 * stream interface - writing to socket
 * @param s - is the input buffer
 * @param n - is the size of the output buffer
 * @return - the number of bytes sent or -1 for error
 * **/
std::streamsize unixSocket::write( const char* s, std::streamsize n )
{
  int bytesSent = 0;
  do
#ifdef PLATFORM_MAC
    bytesSent = send( socketfd, s, n, 0); // block the SIGPIPE signal - for MAC this is a SO_NOSIGPIPE
#else
    bytesSent = send( socketfd, s, n, MSG_NOSIGNAL ); // block the SIGPIPE signal - will receive a EPIPE error on socket closure by the remote end
#endif
  while( ( bytesSent  == -1 ) && ( errno == EINTR ) );
  if( bytesSent == -1 )
  {
    log.warn( log.LOGMOSTLY, "unixSocket::write error on fd %d - %s", socketfd, strerror( errno ) );
    return -1;
  } // if( bytesSent
  else
  {
    if( log.wouldLog( log.LOGSELDOM ) ) log.debug( log.LOGONOCCASION ) << "fd:" << socketfd << " write len " << n << " bytesSent " << bytesSent << " data:'" << s << "'";
    else if( log.wouldLog( log.LEVEL8 ) ) log.debug( log.MIDLEVEL, "fd:%d write len %d bytesSent %d", socketfd, n, bytesSent );
    return bytesSent;
  } // else( byteSent
} // write

/**
 * stream interface - writing to socket
 * @param fd - the socket fd to use
 * @param s - is the input buffer
 * @param bPipe - true to write to a pipe rather than a socket
 * @return - the number of bytes sent or -1 for error
 * **/
std::streamsize unixSocket::writeOnce( int fd, const std::string& s, bool bPipe )
{
  int bytesSent = 0;
  do
  {
    if( bPipe )
      bytesSent = ::write( fd, (const void*)s.c_str(), s.length() );
    else
#ifdef PLATFORM_MAC
      bytesSent = send( fd, s.c_str(), s.length(), 0 ); // block the SIGPIPE signal - for MAC this is a SO_NOSIGPIPE
#else
      bytesSent = send( fd, s.c_str(), s.length(), MSG_NOSIGNAL ); // block the SIGPIPE signal - will receive a EPIPE error on socket closure by the remote end 
#endif
  }
  while( ( bytesSent  == -1 ) && ( errno == EINTR ) );
  if( bytesSent == -1 )
  {
    pStaticLogger->info( loggerDefs::LOGMOSTLY, "writeOnce error on fd %d - %s", fd, strerror(errno) );
    return -1;
  } // if( bytesSent
  else
  {
    if( pStaticLogger->wouldLog( pStaticLogger->LOGSELDOM ) ) pStaticLogger->debug( pStaticLogger->LOGONOCCASION ) << "writeOnce fd:" << fd << " write len " << s.length() << " bytesSent " << bytesSent << " data:'" << s << "'";
    else if( pStaticLogger->wouldLog( pStaticLogger->LEVEL8 ) ) pStaticLogger->debug( pStaticLogger->MIDLEVEL, "writeOnce fd:%d write len %d bytesSent %d", fd, s.length(), bytesSent );
    return bytesSent;
  } // else
} // writeOnce

/**
 * unix domain datagram interface capable of sending on an unconnected socket
 * @param fd - the socket fd to use
 * @param s - is the input buffer
 * @param dest - unix domain path / destination address - if empty the socket has to be connected
 *
 * @return - the number of bytes sent or -1 for error
 * **/
std::streamsize unixSocket::writeOnceTo( int fd, const std::string& s, const std::string& dest )
{
  int bytesSent = 0;
  do
  {
    if( !dest.empty() )
    {
      struct sockaddr_un servAddr;
      memset( &servAddr, 0, sizeof(servAddr) );
      servAddr.sun_family = AF_LOCAL;
      strcpy( servAddr.sun_path, dest.c_str() );
      bytesSent = sendto( fd, s.c_str(), s.length(), 0, (const sockaddr*)&servAddr, sizeof(servAddr) );
    } // if
    else
      bytesSent = send( fd, s.c_str(), s.length(), 0 ); // block the SIGPIPE signal - will receive a EPIPE error on socket closure by the remote end 
  }
  while( ( bytesSent  == -1 ) && ( errno == EINTR ) );
  if( bytesSent == -1 )
  {
    pStaticLogger->info( loggerDefs::LOGMOSTLY, "writeOnceTo error on fd %d - %s", fd, strerror(errno) );
    return -1;
  } // if( bytesSent
  else
  {
    if( pStaticLogger->wouldLog( pStaticLogger->LEVEL8 ) ) pStaticLogger->debug( pStaticLogger->MIDLEVEL, "writeOnceTo fd:%d write len %d bytesSent %d", fd, s.length(), bytesSent );
    return bytesSent;
  } // else

  return bytesSent;
} // writeOnceTo

/**
 Sets the FD_CLOEXEC file descriptor flag on the socket
 @param bSet - true to set the FD_CLO_EXEC flag
 @param fd - file descriptor
 */
void unixSocket::setCloseOnExec( bool bSet, int fd )
{
  int opts = (int)bSet;
  if( fcntl( fd, F_SETFD, opts ) < 0 ) 
    staticLogger.warn( staticLogger.LOGALWAYS, "setCloseOnExec failed on fd:%d bSet:%d - %s", fd, bSet, strerror(errno) );
} // setCloseOnExec

/**
 Sets the FD_CLOEXEC file descriptor flag on the socket
 @param bSet - true to set the FD_CLO_EXEC flag
 */
void unixSocket::setCloseOnExec( bool bSet )
{
  int opts = (int)bSet;
  if( fcntl( socketfd, F_SETFD, opts ) < 0 ) 
    log.warn( log.LOGALWAYS, "setCloseOnExec failed bSet:%d - %s", bSet, strerror(errno) );
} // setCloseOnExec

/**
 Sets the socket into non-blocking mode
 */
void unixSocket::setNonblocking( )
{
  int opts;

  opts = fcntl( socketfd, F_GETFL );
  if( opts < 0 ) 
  {
    log.warn( log.LOGALWAYS, "setNonblocking failed to retrieve opts - %s", strerror( errno ) );
    return;
  } // if
  opts = opts | O_NONBLOCK;
  if( fcntl( socketfd, F_SETFL, opts ) < 0 ) 
    log.warn( log.LOGALWAYS, "setNonblocking failed to set opts - %s", strerror( errno ) );
} // setnonblocking

/**
 Suppresses SIGPIPE on Mac
 */
void unixSocket::setNoSigPipe( )
{
#ifdef PLATFORM_MAC
  int reuse = 1;
  if( setsockopt( socketfd, SOL_SOCKET, SO_NOSIGPIPE, &reuse, sizeof( int ) ) < 0 )
    log.warn( log.LOGALWAYS, "setNoSigPipe failed - %s", strerror( errno ) );
#endif
} // setNoSigPipe

/**
 * inits the polling structure for multiple file descriptors
 * @param numFds - max number of file descriptors that can be accomodated in the wait
 * **/
void unixSocket::initPoll( int numFds )
{
  if( pollFd != NULL ) delete[] pollFd;
  log.info( log.LOGMOSTLY, "initPoll: reserving space for %d entries", numFds );
  numPollFdEntries = numFds;
  pollFd = new struct pollfd[numPollFdEntries];
  memset( pollFd, 0, numPollFdEntries*sizeof( struct pollfd ) );
  pollFdCount = 0;
} // initPoll

/**
 * add a file handle to the poll structure
 * **/
void unixSocket::addReadFd( int fd )
{
  if( pollFdCount >= numPollFdEntries)
  {
    log.error( "addReadFd: failed to add fd %d - out of space (%d entries)", fd, numPollFdEntries );
    return;
  } // if
  
  pollFd[pollFdCount].fd = fd;
  pollFd[pollFdCount++].events = POLLIN;
} // addReadFd

/**
 * waits in blocking mode for a message / event from any of the sockets
 * does not restart after a signal
 * @return true if there are available fds for reading - use getNextFd() to retrieve
 * @exception on error
 * **/
bool unixSocket::multiFdWaitForEvent( )
{
  numPollFdsProcessed = 0;
  numPollFdsAvailable = 0;
  lastFdProcessed = -1;
  lastErrorFd = -1;

  if( log.wouldLog( log.LEVEL8 ) )
  {
    std::string strFds;
    for( int i = 0; i < pollFdCount; i++ )
    {
      char str[32];
      sprintf( str, "%d,", pollFd[i].fd );
      strFds.append( str );
    };
    log.debug( log.LOGALWAYS, "multiFdWaitForEvent: waiting on fds %s", strFds.c_str() );
  } // if

  // wait pollTimeout (in blocking mode) for any events
  int retVal = 0;
  retVal = poll( pollFd, pollFdCount, pollTimeout );
  log.generateTimestamp();

  if( retVal == -1 )
  {
    if( errno == EINTR )
    { 
      log.debug( log.LEVEL8, "multiFdwaitForEvent: we have been interrupted but no new messages" );
      return false;  // we have been interrupted but no new messages
    }
    throw Exception( log, log.ERROR, "multiFdwaitForEvent returned -1" );
  } // if

  numPollFdsAvailable = retVal;
  if( log.wouldLog( log.LEVEL8 ) )
  {
    std::string strFds;
    for( int i = 0; i < pollFdCount; i++ )
    {
      if( pollFd[i].revents & (POLLIN|POLLPRI|POLLERR|POLLHUP|POLLNVAL) )
      {
        char str[32];
        sprintf( str, "%d,", pollFd[i].fd );
        strFds.append( str );
      } // if
    };
    log.debug( log.LOGALWAYS, "multiFdWaitForEvent: %d fds (%s) available", retVal, strFds.c_str() );
  } // if
  else
    log.debug( log.LOGONOCCASION, "multiFdWaitForEvent %d fds available", retVal );
  if( numPollFdsAvailable > 0 )
    return true;
  else
    return false;
} // multiFdWaitForEvent

/**
 * @return the next available file descriptor or -1 if none
 * **/
int unixSocket::getNextFd( )
{
  log.debug( log.LEVEL8, "getNextFd: numPollFdsProcessed %d numPollFdsAvailable %d lastFdProcessed %d", numPollFdsProcessed, numPollFdsAvailable, lastFdProcessed );
  if( numPollFdsProcessed == numPollFdsAvailable ) return -1;
  bool bFound = false;
  while( !bFound && ( ++lastFdProcessed < pollFdCount ) )
  {
    if( pollFd[lastFdProcessed].revents & (POLLIN|POLLPRI) )
    {
      bFound = true;
      numPollFdsProcessed++;
      return pollFd[lastFdProcessed].fd;
    } // if
    else if( pollFd[lastFdProcessed].revents & (POLLERR|POLLHUP|POLLNVAL) )
    {
      int fd = pollFd[lastFdProcessed].fd;
      lastErrorFd = fd;
      if( pollFd[lastFdProcessed].revents & POLLERR )
        log.warn( log.LOGALWAYS, "getNextFd: POLLERR on fd %d", fd );
      else if( pollFd[lastFdProcessed].revents & POLLHUP )
        log.warn( log.LOGALWAYS, "getNextFd: POLLHUP on fd %d", fd );
      else if( pollFd[lastFdProcessed].revents & POLLNVAL )
        log.warn( log.LOGALWAYS, "getNextFd: POLLNVAL on fd %d", fd );

      // should really be removed from the array
      pollFd[lastFdProcessed].events = 0;
      close( fd );
    } // else if
  } // while
  return -1;
} // getNextFd

/**
 * **/
const char* unixSocket::eEventTypeToStr( eEventType e )
{
  switch( e )
  {
    case ET_OTHER:
      return "ET_OTHER";
      break;
    case ET_QUEUE_EVENT:
      return "ET_QUEUE_EVENT";
      break;
    case ET_WORKER_RET:
      return "ET_WORKER_RET";
      break;
    case ET_SIGNAL:
      return "ET_SIGNAL";
      break;
    case ET_LISTEN:
      return "ET_LISTEN";
      break;
    case ET_WORKER_PIPE:
      return "ET_WORKER_PIPE";
      break;
    default:
      return "eEventType not recognised";
      break;
  } // switch
} // eEventTypeToStr

