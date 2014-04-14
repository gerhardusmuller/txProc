/**
 Network class eventually to replace in part networkIf.*
 
 $Id: network.cpp 2629 2012-10-19 16:52:17Z gerhardus $
 Versioning: a.b.c a is a major release, b represents changes or new features, c represents bug fixes. 
 @version 1.0.0		04/09/2012		Gerhardus Muller		Script created

 @note

 @todo
 implement unix domain stream sockets - implemented - to be tested
 need to detect the closure event and hook it for unix domain stream sockets - implemented to be be tested
 
 @bug

	Copyright Notice
 * **/

#include "src/options.h"
#include "nucleus/network.h"
#include "nucleus/optionsNucleus.h"
#include <sys/socket.h>
#include <sys/un.h>
#include <sys/stat.h>
#include <netdb.h>
#include <arpa/inet.h>
#include <pwd.h>
#include <grp.h>
#include <errno.h>

network::network( )
  : object( "network" )
{
  listenUnFd = -1;
  listenUnStreamFd = -1;
  pUnSock = NULL;
  pUnStreamSock = NULL;
  pEpollRd = NULL;
  pEpollWr = NULL;
  init();
} // network

/**
 * destructor
 * **/
network::~network( )
{
  if( pEpollRd != NULL ) delete pEpollRd;
  if( pEpollWr != NULL ) delete pEpollWr;
  if( pUnSock != NULL ) delete pUnSock;
  if( pUnStreamSock != NULL) delete pUnStreamSock;
  if( listenUnFd != -1 ) close( listenUnFd );
  if( listenUnStreamFd != -1 ) close( listenUnStreamFd );

  socketMapIteratorT it;
  if( !tcpFds.empty() )
  {
    for( it = tcpFds.begin(); it != tcpFds.end(); it++ )
    {
      log.debug( log.MIDLEVEL, "~network closing fd:%d", it->first );
      delete( it->second );
      close( it->first );
    } // for
    tcpFds.erase( tcpFds.begin(), tcpFds.end() );
  } // if
} // ~network

/**
 * init
 * **/
void network::init( )
{
  log.setAddPid( true );

  // create the polling instance
  resetRdPollMap();
  resetWrPollMap();

  // create a Unix domain networkIf
  // for a stream networkIf create only a listen networkIf and accept in the same way as TCP
  // note that rebuildPollList() has to be altered to reflect the permanent listen networkIf as well
  if( pOptionsNucleus->unixSocketPath.length() > 0 )
  {
    listenUnFd = createUnListenSocket( SOCK_DGRAM, pOptionsNucleus->unixSocketPath.c_str() );
    pUnSock = new unixSocket( listenUnFd, unixSocket::ET_QUEUE_EVENT, true, "unFd" );
    pUnSock->setNonblocking( );
  } // if
  if( pOptionsNucleus->unixSocketStreamPath.length() > 0 )
  {
    listenUnStreamFd = createUnListenSocket( SOCK_STREAM, pOptionsNucleus->unixSocketStreamPath.c_str() );
    pUnStreamSock = new unixSocket( listenUnStreamFd, unixSocket::ET_LISTEN, true, "unStreamFd" );
  } // if
  log.info( log.LOGALWAYS, "init: Unix domain fd:%d Unix domain stream listen fd:%d", listenUnFd, listenUnStreamFd );

  char hostname[HOST_NAME_MAX];
  hostname[0] = '\0';
  gethostname( hostname, HOST_NAME_MAX );
  hostId = hostname;
  char greetingStr[128];
  sprintf( greetingStr, "%s@%s pver %s md %d", pOptions->APP_BASE_NAME, hostname, baseEvent::PROTOCOL_VERSION_NUMBER, unixSocket::READ_BUF_SIZE );
  greetingString = greetingStr;
  log.info( log.LOGALWAYS, "init: hostname:'%s' greeting:'%s'", hostname, greetingStr );
} // init

/**
 * deletes and re-creates the read poll map
 * **/
void network::resetRdPollMap( )
{
  if( pEpollRd != NULL ) delete pEpollRd;
  pEpollRd = new epoll( pOptionsNucleus->maxNetworkDescriptors );
  pEpollRd->setLoggingId( "pEpollRd" );
} // resetRdPollMap

/**
 * deletes and re-creates the write poll map
 * **/
void network::resetWrPollMap( )
{
  if( pEpollWr != NULL ) delete pEpollWr;
  pEpollWr = new epoll( pOptionsNucleus->maxNetworkDescriptors );
  pEpollWr->setLoggingId( "pEpollWr" );
} // resetWrPollMap

/**
 * builds the read polling map
 * **/
void network::buildRdPollMap( )
{
  if( listenUnFd != -1 ) addRdFd( pUnSock );
  if( listenUnStreamFd != -1 ) addRdFd( pUnStreamSock );

  socketMapIteratorT it;
  if( !tcpFds.empty() )
  {
    for( it = tcpFds.begin(); it != tcpFds.end(); it++ )
      addRdFd( it->second );
  } // if
} // buildRdPollMap

/**
 * adds a file descriptor to the read poll map
 * **/
void network::addRdFd( unixSocket* pSock )
{
  int fd = pSock->getSocketFd();
  pEpollRd->addFd( fd, (void*)pSock );
} // addRdFd

/**
 * adds a file descriptor to the write poll map
 * **/
void network::addWrFd( unixSocket* pSock )
{
  int fd = pSock->getSocketFd();
  pEpollWr->addFd( fd, (void*)pSock );
} // addWrFd

/**
 * removes a file descriptor to the read poll map
 * **/
void network::deleteRdFd( int fd )
{
  pEpollRd->deleteFd( fd );
} // deleteRdFd

/**
 * removes a file descriptor to the write poll map
 * **/
void network::deleteWrFd( int fd )
{
  pEpollWr->deleteFd( fd );
} // deleteWrFd

/**
 * create an Unix domain networkIf for listening on
 * adjust the ownership / access permissions
 * @param type SOCK_STREAM, SOCK_DGRAM
 * @param networkIfPath
 * @param Qlen
 * @return the file descriptor for the initialised networkIf
 * @exception on error
 * **/
int network::createUnListenSocket( int type, const char* networkIfPath, int qlen )
{
  int fd;
  struct sockaddr_un un;

  if( ( unlink( networkIfPath ) < 0 ) && ( errno != ENOENT ) )
    log.error( "unlink of %s failed: %s", networkIfPath, strerror(errno) );

  un.sun_family = AF_UNIX;
  strcpy( un.sun_path, networkIfPath );
  if( ( fd = ::socket( AF_UNIX, type, 0 ) ) < 0 )
    throw Exception( log, log.ERROR, "createUnListenSocket networkIf error: %s", strerror(errno) );

  int size = offsetof( struct sockaddr_un, sun_path ) + strlen( un.sun_path );
  if( bind( fd, (struct sockaddr *)&un, size ) < 0 )
    log.error( "init failed to bind address %s to unix networkIf %d: %s", networkIfPath, fd, strerror(errno) );
  else
    log.info( log.LOGMOSTLY, "bound unix domain networkIf %d to address %s", fd, networkIfPath );

  if( type == SOCK_STREAM )
  {
    if( listen( fd, qlen ) < 0 )
      throw Exception( log, log.ERROR, "createUnListenSocket listen error: %s", strerror(errno) );
  } // if

  // adjust the ownership / access permissions to be that of the user / group we are running as
  if( chmod( un.sun_path, S_IRUSR | S_IWUSR | S_IXUSR | S_IRGRP | S_IWGRP | S_IXGRP ) < 0 )
    log.warn( log.LOGALWAYS, "createUnListenSocket chmod failed: %s", strerror(errno) );
  
  struct passwd* pw = getpwnam( pOptionsNucleus->runAsUser.c_str() );
  if( pw == NULL ) 
  {
    log.error( "createUnListenSocket getpwnam could not find user %s", pOptionsNucleus->runAsUser.c_str() );
  } // if
  else
  {
    int userId = pw->pw_uid;
    int groupId = pw->pw_gid; // use this as a default fallback
    struct group* gr = getgrnam( pOptionsNucleus->socketGroup.c_str() );
    if( gr == NULL )
      log.error( "createUnListenSocket getgrnam could not find group %s using default %d", pOptionsNucleus->socketGroup.c_str(), groupId );
    else
      groupId = gr->gr_gid;
    
    if( chown( un.sun_path, userId, groupId ) < 0 )
      log.warn( log.LOGALWAYS, "createUnListenSocket chown (user: %d, group: %d) failed: %s", userId, groupId, strerror( errno ) );
    else
      log.info( log.LOGALWAYS, "createUnListenSocket chown (user: %d, group: %d)", userId, groupId );
  } // else

  return fd;
} // createUnListenSocket

/**
 * accepts a listen event - for the moment only geared to work for unix domain stream sockets
 * **/
void network::listenEvent()
{
  int newUnFd = acceptUnSocket();
  if( newUnFd > 0 )
  {
    unixSocket* pSock = new unixSocket( newUnFd, unixSocket::ET_QUEUE_EVENT );
    pSock->setNonblocking();
    tcpFds.insert( std::pair<int,unixSocket*>( newUnFd, pSock ) );
    log.info( log.MIDLEVEL, "accepted new unix fd %d", newUnFd );
    writeGreeting( pSock );
    addRdFd( pSock );
    addWrFd( pSock );
  } // if
  else
    log.error( "listenEvent: acceptUnSocket should have returned a valid fd not %d", newUnFd );
} // listenEvent

/**
 * adapted from Stevens and Rago
 * accepts a new connection on the listening Unix domain networkIf
 * Obtain the client's user ID from the pathname that it must bind to before
 * calling us
 * @return the new fd
 * @exception on error
 * **/
int network::acceptUnSocket( )
{
  int fd;
  socklen_t len;
  struct sockaddr_un  un;
  
  len = sizeof( un );
  //log.debug( log.LOGNORMAL, "acceptUnSocket struct len %d", len );
  while( ((fd = accept(listenUnFd,(struct sockaddr *)&un,&len)) < 0) && (errno == EINTR) );
  if( fd < 0 ) throw Exception( log, log.ERROR, "acceptUnSocket accept error: %s", strerror(errno) );

  return fd;
} // acceptUnSocket

/**
 * closes and cleans up on a stream socket
 * **/
void network::closeStreamSocket( int fd )
{
  socketMapIteratorT it;
  it = tcpFds.find( fd );
  if( it != tcpFds.end( ) )
  {
    log.info( log.LOGNORMAL, "closeStreamSocket fd:%d", fd );
    unixSocket* pSock = it->second;
    tcpFds.erase( it );
    deleteRdFd( fd );
    deleteWrFd( fd );
    close( fd );
    delete pSock;
  } // if
  else
  {
    log.warn( log.LOGALWAYS, "closeStreamSocket fd:%d not found", fd );
  } // else
} // closeStreamSocket

/**
 * write an initial greeting to an external stream networkIf connection
 * @param pSocket
 * **/
void network::writeGreeting( unixSocket* pSocket )
{
  char str[1024];
  snprintf( str, 1024, "%03d:%s", (int)greetingString.length(), greetingString.c_str() );
  pSocket->write( str, strlen( str ) );
} // writeGreeting
