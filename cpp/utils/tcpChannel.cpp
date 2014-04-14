/** @class tcpChannel
 Implementation of a TCP channel
 Note that although the code originally worked for both client and server 
 the server part has not been tested again
 
 $Id: tcpChannel.cpp 1 2009-08-17 12:36:47Z gerhardus $
 Versioning: a.b.c  a is a major release, b represents changes or new additions, c is a bug fix
 @version 1.0.0   19/04/2004    Gert Muller         Script created
 @version 1.0.1   18/10/2004    Gert Muller         Changed the write/receive interface to use char pointers
 @version 2.0.0   14/05/2009    Gerhardus Muller    Adapted for use in the txDelta framework
 @version 2.1.0		11/06/2009		Gerhardus Muller		read throws on socket eof condition
 
 @note
 
 @todo
 
 @bug
 
 Copyright notice
 */
 
#include "utils/tcpChannel.h"
#include "utils/utils.h"
#include "exception/Exception.h"
#include <memory.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <fcntl.h>


/**
 Constructor - empty for the base class
 */
tcpChannel::tcpChannel() : channel( "tcpChannel" )
{
  channelType = "TCP";
  remoteSocket = -1;
  listenSocket = -1;
  bServer = false;
  bAccepted = false;
  bConnected = false;
  ailist = NULL;
  localBuffer = NULL;
  localBufferLen = 0;
  bBlocking = true;
  bThrowEof = false;
} // tcpChannel

/**
 Destructor - empty for the base class
 */
tcpChannel::~tcpChannel()
{
  close( );
  if( ailist != NULL ) freeaddrinfo( ailist );
    
  if( listenSocket != 0 )
    ::shutdown( listenSocket, SHUT_RDWR );

  if( localBuffer != NULL ) delete[] localBuffer;
} // ~tcpChannel

/**
 Standard logging call - produces a generic text version of the object.
 Memory allocation / deleting is handled by this object.
 @return pointer to a string describing the state of the object.  The string has an unspecified lifetime and is not multi-thread safe
 */
std::string tcpChannel::toString( )
{
  std::ostringstream oss;
  oss << channel::toString();
  oss << " port:" << remotePort << " remoteIPaddress:" << remoteIPaddress << " bServer:" << bServer << " bAccepted:" << bAccepted << " bConnected:" << bConnected << " fd:" << remoteSocket;
	return oss.str();
}	// toString

/**
 Object initialisation - assumes all parameters have been configured.
 @exception Exception
 */
void tcpChannel::init( )
{
  // create the socket and the address required for sendto
  // the same socket is used for sending and receiving
  // for receiving bind to a well defined port
  remoteSocket = createTCPSocket( );
  listenSocket = remoteSocket;
  if( bServer )
  {
    readFileDescriptor = listenSocket;    // this is required to enable the manager to listen for a new connection for a server socket
    bindTCPSocket( listenSocket );
    if( listen( remoteSocket, 1 ) != 0 )
      throw Exception( log, log.ERROR, "init:%s", strerror(errno) );
  } // if
  else
  {
    readFileDescriptor = remoteSocket;    // this is required to enable the manager to listen for data available
    try
    {
      connect();
    } // try
    catch( Exception *e )
    {
    } // catch
  } // else

  log.info( log.LOGMOSTLY, "init: %s", toString().c_str() );
} // init
 
/**
 Initialise from the given ini file object with the given section name.
 @param isServer - true if server, false if client
 @param ip - required for client, otherwise empty
 @param port - required for both server and client
 @exception Exception on failure to properly initialise
 */
void tcpChannel::init( bool isServer, const std::string& ip, const std::string& port )
{
  bServer = isServer;
  lookupAddress( ip.c_str(), port.c_str() );

  remoteIPaddress = ip;
  remotePort = port;
  //bBlocking = false;
  init();
} // init

/**
 * lookup addresses
 * stores the result into remoteIPaddress / remotePort
 * @param ip - required for client, otherwise NULL
 * @param port - required for both server and client
 * @exception
 * **/
void tcpChannel::lookupAddress( const char* ip, const char* port )
{
  // translate the address to number form 
  struct addrinfo hint;
  hint.ai_flags = 0;
  hint.ai_family = 0;
  hint.ai_socktype = SOCK_STREAM;
  hint.ai_protocol = 0;
  hint.ai_addrlen = 0;
  hint.ai_canonname = NULL;
  hint.ai_addr = NULL;
  hint.ai_next = NULL;

  if( ailist != NULL )
  {
    freeaddrinfo( ailist );
    ailist = NULL;
  } //if

  int err;
  if( ( err = getaddrinfo( ip, port, &hint, &ailist ) ) != 0 )
    throw Exception( log, log.ERROR, "lookupAddress: ip:%s port:%s err:%s", (ip==NULL?"NULL":ip), (port==NULL?"NULL":port), gai_strerror(err) );
} // lookupAddress

/**
 Writes the packet to the TCP port. Invoke tx done callback immediately
 Closes the socket if an error occurs during send.  Attempts to connect if not in server
 mode and not yet connected.
 @param buffer - buffer to transmit
 @param bufLen - number of characters in the buffer
 @return - the number of characters actually transmitted (accepted by the networking layer) or 0 if not connected and failed to connect.
 */
int tcpChannel::write( const char* buffer, int bufLen )
{
  if( bServer && !bAccepted )
    throw Exception( log, log.ERROR, "write: server socket not yet accepted" );
  if( !bServer && !bConnected )
  {
    try
    {
      connect();
    }
    catch( Exception* e )
    {
    }
    return 0;
  } // if
  
  int bytesSent = 0;
  do
    bytesSent = send( remoteSocket, buffer, bufLen, 0 );
  while( ( bytesSent  == -1 ) && ( errno == EINTR ) );
  if( ( bytesSent < 0 ) || ( bufLen != bytesSent ) )
  {
    log.warn( log.LOGMOSTLY, "write: fd:%d bufLen:%d bytesSent:%d err:%s", remoteSocket, bufLen, bytesSent, strerror(errno) );
    close();
    return 0;
  } // if
  else
  {
    if( log.wouldLog( log.LOGSELDOM ) )
    {
      std::string str( buffer, bufLen );
      utils::stripTrailingCRLF( str );
      log.debug( log.LOGONOCCASION ) << "fd:" << remoteSocket << " write len:" << bufLen << " bytesSent:" << bytesSent << " data:'" << str << "'";
    } // if
    else if( log.wouldLog( log.LOGONOCCASION ) ) log.debug( log.MIDLEVEL, "fd:%d write len:%d bytesSent:%d", remoteSocket, bufLen, bytesSent );

    if( txDoneCallback != NULL )
      (*txDoneCallback)( channelID );
    return bytesSent;
  } // else bytesSent < 0
} // write
/**
 * writes a message on the channel
 * @param mess - to write
 * @return the number of characters written
 * **/
int tcpChannel::write( const std::string& mess )
{
  return write( mess.c_str(), mess.length() );
} // write

/**
 * reads a message of max length localBufferLen
 * @return the message
 * **/
std::string& tcpChannel::receive( )
{
  if( localBuffer == NULL ) allocLocalBuffer( DEF_BUF_LEN );
  int numBytes = receive( localBuffer, localBufferLen );
  if( numBytes > 0 )
    readBuf.assign( localBuffer, numBytes );
  else
    readBuf.erase();
  return readBuf;
} // receive

/**
 Operation depends on the mode of the socket.  For a client socket it receives a TCP packet.
 For a server socket it initially accepts a new connection and on subsequent calls receives data.
 Invokes the character received callback if defined and data has been received.
 Closes the socket if an error occurs during recv.  If not a server and not yet connected attempts 
 to connect before calling recv.  Note if this happens the recv will block.
 This function only reads as many bytes as is presented in a single read. It does not
 attempt to read bufLen characters
 @param buffer - buffer into which the received data must be copied
 @param bufLen - size of the receive buffer - ie max length of the message
 @return - the number of bytes that has been received.
 @exception - Exception on failure to connect or eof if configured via setThrowEof()
 */
int tcpChannel::receive( char* buffer, int bufLen )
{
  if( !bServer && !bConnected )
  {
    try
    {
      connect();
    }
    catch( Exception* e )
    {
    }
    return 0;
  } // if
  if( bServer && !bAccepted )
  {
    acceptConnection();    // subsequent selects and receives will now use remoteSocket rather than listenSocket
    return 0;
  } // if

  int bytesReceived = 0;
  do 
    bytesReceived = recv( remoteSocket, buffer, bufLen, 0 );
  while( (bytesReceived == -1) && (errno == EINTR) );
  if( (bytesReceived == -1) && (errno == EAGAIN) )
    return 0;

  if( ( bytesReceived < 0 ) || (bytesReceived == 0) )
  {
    log.warn( log.LOGMOSTLY, "receive: fd:%d bytesReceived:%d err:%s", remoteSocket, bytesReceived, strerror(errno) );
    close();
    if( bThrowEof ) throw Exception( log, log.ERROR, "read: eof on fd:%d", remoteSocket );
    return 0;
  } // if
  else
  {
    if( log.wouldLog( log.LOGSELDOM ) )
    {
      std::string str( buffer, bytesReceived );
      utils::stripTrailingCRLF( str, true );
      log.debug( log.LOGONOCCASION ) << "receive: fd:" << remoteSocket << " bufLen:" << bufLen << " bytesReceived:" << bytesReceived << " data:'" << str << "'";
    } // if
    else if( log.wouldLog( log.LOGONOCCASION ) ) log.debug( log.MIDLEVEL, "receive: fd:%d bufLen:%d bytesReceived:%d", remoteSocket, bufLen, bytesReceived );
    return bytesReceived;

    if( ( bytesReceived > 0 ) && ( rxMessCallback != NULL ) )
      rxMessCallback->rxMessCallback( channelID, buffer, bytesReceived );

    if( ( bytesReceived > 0 ) && ( rxCharCallback != NULL ) )
    {
      for( int i = 0; i < bytesReceived; i++ )
        (*rxCharCallback)( channelID, ((char*)buffer)[i] );
    } // if

    return bytesReceived;
  } // else bytesReceived < 0
} // receive

/**
 Accepts a new connection.  remoteSocket contains this socket.
 Switches readFileDescriptor to now point to the remoteSocket.
 Will block if no connection is pending.
 @exception - ChannelException on failure to accept
 */
void tcpChannel::acceptConnection( )
{
  struct sockaddr_in netclient;
  socklen_t len = sizeof( struct sockaddr );

  if( ( remoteSocket = accept( listenSocket, (struct sockaddr *)(&netclient), &len ) ) == -1 )
  {
    throw Exception( log, log.ERROR, "acceptConnection:%s", strerror(errno) );
  }
  else
  {
    readFileDescriptor = remoteSocket;    // this is required to enable the manager to listen for data available
    bAccepted = true;
  }
  log.info( log.LOGNORMAL, "acceptConnection:%s", toString().c_str() );
} // acceptConnection

/**
 Connects to a peer - for a client.  The socket is in non-blocking mode and therefor after
 trying to connect the socket returns a connection in progress code.
 */
void tcpChannel::connect( )
{
  int retcode = 0;
  if( (retcode=::connect( remoteSocket, ailist->ai_addr, ailist->ai_addrlen ) ) == -1 )
  {
    if( errno == ECONNREFUSED )
      close();
    else
      readFileDescriptor = remoteSocket;  // we can potentially read soon again
    lastError = strerror(errno);
    throw Exception( log, log.ERROR, "connect:%s", lastError.c_str() );
  }
  bConnected = true;
  log.info( log.LOGNORMAL, "connect:%s", toString().c_str() );
} // connect

/**
Closes the socket. If a server socket it returns to listening mode
*/
void tcpChannel::close( )
{
	int ret = ::shutdown( remoteSocket, SHUT_RDWR );
	if( ret != 0 ) log.warn( log.LOGALWAYS, "close: shutdown error: %s", strerror( errno ) );

	ret = ::close( remoteSocket );
	if( ret != 0 ) log.warn( log.LOGALWAYS, "close: close error: %s", strerror( errno ) );

	bAccepted = false;
	bConnected = false;

	if( bServer )
	{
		readFileDescriptor = listenSocket; // this is required to enable the manager to listen for a new connection for a server socket
	} // if
	else
	{
		remoteSocket = createTCPSocket( );
		readFileDescriptor = 0; // remove from the select list
	} // else

  log.info( log.LOGNORMAL, "close:%s", toString().c_str() );
} // close

/**
 Sets up an internet address structure.
 @param  addr  Address to set up.
 @param  ip    IP address (dotted notation) to set it as.  NULL for localhost.
 @param  port  Port to use, 0 for don't care.
 */
void tcpChannel::createAddress( struct sockaddr_in &addr, const char* ip, int port )
{
  memset( &addr, 0, sizeof( addr ) );
  addr.sin_family = AF_INET;
  addr.sin_port   = htons( port );
  
  if (ip == NULL)
    addr.sin_addr.s_addr = htonl( INADDR_ANY );
  else
    addr.sin_addr.s_addr = inet_addr( ip );
} // createAddress

/**
 Creates a TCP socket
 @return File descriptor representing the socket or -1 for error.
 @exception Exception
 */
int tcpChannel::createTCPSocket( )
{
  int fd;
  
  fd = socket( PF_INET, SOCK_STREAM, IPPROTO_TCP );
  if ( fd == -1 ) throw Exception( log, log.ERROR, "createTCPSocket:%s", strerror(errno) );

  if( !bBlocking )
    setnonblocking( fd );
  return fd;
} // createTCPSocket

/**
  Binds a TCP socket for listening
  @param fd the file descriptor to bind
  @exception Exception
  */
void tcpChannel::bindTCPSocket( int fd )
{
  if( bind( fd, ailist->ai_addr, ailist->ai_addrlen ) < 0 )
    throw Exception( log, log.ERROR, "bindTCPSocket:%s", strerror(errno) );
} // bindTCPSocket

/**
 Sets the socket into non-blocking mode
 @param sock - socket descriptor
 */
void tcpChannel::setnonblocking( int sock )
{
  int opts;

  opts = fcntl(sock,F_GETFL);
  if( opts < 0 )
    throw Exception( log, log.ERROR, "setnonblocking:%s", strerror(errno) );

  opts = (opts | O_NONBLOCK);
  if( fcntl(sock,F_SETFL,opts) < 0 )
    throw Exception( log, log.ERROR, "setnonblocking:%s", strerror(errno) );
} // setnonblocking


