/** @class networkIf
 networkIf - provides a networking interface to the outside world
 
 $Id: networkIf.cpp 2931 2013-10-16 09:47:29Z gerhardus $
 Versioning: a.b.c a is a major release, b represents changes or new features, c represents bug fixes. 
 @version 1.0.0		11/11/2009		Gerhardus Muller		script created
 @version 1.1.0		19/11/2009		Gerhardus Muller		print the event that causes json to throw in dispatchPacket
 @version 1.2.0		24/05/2011		Gerhardus Muller		ported to Mac
 @version 1.3.0		20/03/2012		Gerhardus Muller		added the max data gram size to the greeting string
 @version 1.3.1		14/05/2012		Gerhardus Muller		fixed unixSocket/TCP connection memory leak
 @version 1.4.0		27/02/2013		Gerhardus Muller		select support / fragmented packets on tcp write
 @version 1.5.0		16/10/2013		Gerhardus Muller		tcp listening on any ip or a specific ip

 @note

 @todo
 
 @bug

	Copyright Notice
 */

#include <time.h>
#include <fstream>
#include <iostream>
#include <sstream>
#include <signal.h>
#include <unistd.h>
#include <sys/poll.h>
#include <sys/types.h> 
#include <sys/wait.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <sys/stat.h>
#include <netdb.h>
#include <arpa/inet.h>
#include <pwd.h>
#include <grp.h>
#include <errno.h>
#include "boost/regex.hpp"
#ifndef PLATFORM_MAC 
#include <sys/prctl.h>
#endif

#include "networkIf/networkIf.h"
#include "networkIf/optionsNetworkIf.h"
#include "src/options.h"
#include "utils/utils.h"
#include "nucleus/recoveryLog.h"
#include "nucleus/baseEvent.h"
#include "nucleus/nucleus.h"

bool networkIf::bRunning = true;
bool networkIf::bReopenLog = false;
bool networkIf::bResetStats = false;
bool networkIf::bDump = false;
recoveryLog* networkIf::theRecoveryLog = NULL;
optionsNetworkIf* pOptionsNetworkIf = NULL;
const int networkIf::UN_TYPE = SOCK_DGRAM;    ///< Unix domain networkIf type - can be either SOCK_STREAM or SOCK_DGRAM

/**
 Construction
 @param networkIfFd - unix domain networkIf to submit events to the networkIf - the networkIf reads the [1] side
 @param nucleusFd - file descriptor to talk to the dispatcher
 @param theRecoveryLog
 @param theArgc
 @param theArgv
 */
networkIf::networkIf( int networkIfFd[2], int nucleusFd, int parentFd, recoveryLog* recovery, int theArgc, char* theArgv[] )
  : object( "networkIf" )
{
  pRecSock = NULL;
  fdSendSock = 0;
  fdNucleusSock = 0;
  listenTcpFd = -1;
  listenUdpFd = -1;
  listenUnFd = -1;
  pollFd = NULL;
  numPollFdEntries = 0;
  theRecoveryLog = recovery;
  argc = theArgc;
  argv = theArgv;
  init( networkIfFd, nucleusFd, parentFd );
}	// networkIf

/**
 Destruction
 */
networkIf::~networkIf()
{
  log.debug( log.MIDLEVEL, "~networkIf" );

  // close any open sockets
  if( listenTcpFd != -1 ) close( listenTcpFd );
  if( ( UN_TYPE == SOCK_STREAM ) && ( listenUnFd != -1 ) ) close( listenUnFd );
  if( ( pOptionsNetworkIf->unixSocketPath.length() > 0 ) && ( unlink( pOptionsNetworkIf->unixSocketPath.c_str() ) < 0 ) && ( errno != ENOENT ) )
      log.error( "unlink of %s failed: %s", pOptionsNetworkIf->unixSocketPath.c_str(), strerror( errno ) );
  
  std::map<int,tConnectData*>::iterator it;
  if( !tcpFds.empty() )
  {
    for( it = tcpFds.begin(); it != tcpFds.end(); it++ )
    {
      log.debug( log.MIDLEVEL, "~networkIf closing fd:%d", it->first );
      delete( it->second->pSocket );
      delete( it->second );
      close( it->first );
    } // for
    tcpFds.erase( tcpFds.begin(), tcpFds.end() );
  } // if

  if( pRecSock != NULL ) delete pRecSock;
  if( pollFd != NULL ) delete[] pollFd;

  log.info( log.MIDLEVEL, "~networkIf cleaned up" );
  log.closeLogfile( );
  delete pOptionsNetworkIf;

  baseEvent::staticLogger.shutdownLogging();
  unixSocket::staticLogger.shutdownLogging();
}	// ~networkIf

/**
 Standard logging call - produces a generic text version of the networkIf.
 Memory allocation / deleting is handled by this networkIf.
 @return pointer to a string describing the state of the networkIf.  The string has an unspecified lifetime and is not multi-thread safe
 */
std::string networkIf::toString( )
{
  std::ostringstream oss;
  oss << typeid(*this).name();
	return oss.str();
}	// toString

/**
 * init
 * @param networkIfFd - unix domain socket to submit events to the networkIf - the networkIf reads the [1] side
 * @param nucleusFd - file descriptor to talk to the nucleus process
 * @param parentFd - file descriptor to talk to the parent
 * **/
void networkIf::init( int networkIfFd[2], int nucleusFd, int parentFd )
{
  pOptionsNetworkIf = new optionsNetworkIf();
  bool bDone = !pOptionsNetworkIf->parseOptions( argc, argv );
  if( bDone )
  {
    delete pOptionsNetworkIf;
    return;
  } // if( bDone

  // seed the random number generator
  srandom( time(NULL) );
  
  // configure logging
  log.openLogfile( pOptionsNetworkIf->logFile.c_str(), pOptionsNetworkIf->bFlushLogs );
  log.setDefaultLevel( (loggerDefs::eLogLevel)pOptionsNetworkIf->defaultLogLevel );
  if( pOptionsNetworkIf->bLogStderr )
    log.setLogStdErr( pOptionsNetworkIf->bLogStderr );
  else if( pOptionsNetworkIf->bLogConsole )
    log.setLogConsole( pOptionsNetworkIf->bLogConsole );
  log.setAddPid( true );
  pOptionsNetworkIf->logOptions(); 
  log.setAddExecTrace( true );
  log.generateTimestamp();
  unixSocket::pStaticLogger->init( "unixSocket", (loggerDefs::eLogLevel)pOptionsNetworkIf->defaultLogLevel );
  unixSocket::pStaticLogger->setAddPid( true );

  // register signal handling
  signal( SIGINT, SIG_IGN );    // will always receive a sigterm from parent
//  signal( SIGINT, networkIf::sigHandler );
  signal( SIGTERM, networkIf::sigHandler );
  
  // reset the signal blocking mask that we inherited
  sigset_t blockmask;
  sigset_t oldmask;
  sigemptyset( &blockmask );    // unblock all the signals
  if( sigprocmask( SIG_SETMASK, &blockmask, &oldmask ) < 0 )
    throw Exception( log, log.ERROR, "sigprocmask returned -1" );
  
  // create the networkIf objects
  pRecSock = new unixSocket( networkIfFd[1], unixSocket::ET_QUEUE_EVENT, false, "networkIfFd1" );
  eventSourceFd = networkIfFd[1];
  fdSendSock = networkIfFd[0];
  fdNucleusSock = nucleusFd;
  fdParentSock = parentFd;
  pRecSock->setNonblocking( );
  log.info( log.LOGALWAYS, "init recSock %d, sendSock %d nucleusSock %d", networkIfFd[1], networkIfFd[0], nucleusFd );
  
  // retrieve our hostname
#if !defined( HOST_NAME_MAX )
#define HOST_NAME_MAX 256
#endif
  char hostname[HOST_NAME_MAX];
  hostname[0] = '\0';
  gethostname( hostname, HOST_NAME_MAX );
  hostId = hostname;
  char greetingStr[128];
  sprintf( greetingStr, "%s@%s pver %s md %d", pOptions->APP_BASE_NAME, hostname, baseEvent::PROTOCOL_VERSION_NUMBER, unixSocket::READ_BUF_SIZE );
  greetingString = greetingStr;
  log.info( log.LOGALWAYS, "hostname:'%s' greeting:'%s'", hostname, greetingStr );
  
  // create the listen networkIfs before we drop priviledge
  // if either of these two cannot be bound to createListenSocket will throw and 
  // cause a terminate in execution
  listenTcpFd = createListenSocket( SOCK_STREAM, pOptionsNetworkIf->socketService, pOptionsNetworkIf->listenAddr );
  
  // treat the udp networkIf like an accepted tcp networkIf
  listenUdpFd = createListenSocket( SOCK_DGRAM, pOptionsNetworkIf->socketService, pOptionsNetworkIf->listenAddr );
  unixSocket* pSock = new unixSocket( listenUdpFd, unixSocket::ET_QUEUE_EVENT, true, "udpFd" );
  pSock->setNonblocking( );
  tConnectData* pConnect = new tConnectData;
  pConnect->pSocket = pSock;
  pConnect->bFragmentData = false;
  tcpFds.insert( std::pair<int,tConnectData*>( listenUdpFd, pConnect ) );
  
  // create a Unix domain networkIf
  // for a stream networkIf create only a listen networkIf and accept in the same way as TCP
  // note that rebuildPollList() has to be altered to reflect the permanent listen networkIf as well
//  if( pOptionsNetworkIf->unixSocketPath.length() > 0 )
//  {
//    listenUnFd = createUnListenSocket( UN_TYPE, pOptionsNetworkIf->unixSocketPath.c_str() );
//    if( UN_TYPE == SOCK_DGRAM )
//    {
//      unixSocket* pSock = new unixSocket( listenUnFd, true, "unFd" );
//      pSock->setNonblocking( );
//      tcpFds.insert( std::pair<int,unixSocket*>( listenUnFd, pSock ) );
//    } // if
//  } // if
  
  log.info( log.LOGALWAYS, "init: TCP listen fd:%d, UDP listen fd:%d, Unix domain fd:%d", listenTcpFd, listenUdpFd, listenUnFd );
  
  // create the data structure for poll and insert the starting fds into it
  maxNumPollFdEntries = 4 + pOptionsNetworkIf->maxTcpConnections;   // pRecSock, listenTcpFd, listenUnFd, listenUdpFd + simultaneous open TCP / Unix domain connections
  pollFd = new struct pollfd[maxNumPollFdEntries];
  memset( pollFd, 0, maxNumPollFdEntries*sizeof(struct pollfd) );
  numPollFdEntries = 0;
  
  // if more static entries are added remember to change the hardcoded value in rebuiltPollList( )
  // the udp networkIf must be last - it is inserted into the tcpFds and is therefor re-inserted every
  // time the poll array is rebuilt
  //addFdToPoll( eventSourceFd );
  //addFdToPoll( listenTcpFd );   // should signal when there is a new networkIf to accept
  //addFdToPoll( listenUnFd );    // should signal when there is data to be read or a new networkIf to accept when in SOCK_STREAM mode
  //addFdToPoll( listenUdpFd );   // should signal when there is data to be read
  rebuildPollList( );

  bool bResult = dropPriviledge( pOptionsNetworkIf->runAsUser.c_str() );
  if( !bResult )
  {
    log.error( "failed to drop priviledges to user %s\n", pOptionsNetworkIf->runAsUser.c_str() );
    close( networkIfFd[1] ); // prevent the attempted delivery of events
  } // if
} // init

/**
 * main networkIf processing loop
 * **/
void networkIf::main( )
{
  baseEvent::theRecoveryLog = theRecoveryLog;   // separate process - need to re-init
  bRunning = true;

  while( bRunning )
  {
    log.debug( log.MIDLEVEL, "bRunning %d", networkIf::bRunning );
    try
    {
      bool bReady = waitForEvent( );
      if( bReady )
      {
        int fd = 0;
        short eventType = 0;
        // retrieve the available file descriptors and process the events
        while( ( fd = getNextFd( eventType ) ) > 0 )
        {
          log.generateTimestamp();
          log.debug( log.MIDLEVEL, "main - fd %d has data", fd );
          // service a unix domain networkIf event from one of the other processes
          if( eventType & POLLIN )
          {
            if( fd == eventSourceFd )
            {
              log.debug( log.LOGSELDOM, "about to unserialise" );
              // the event will be deleted at the point of being consumed
              baseEvent* pEvent = baseEvent::unSerialise( pRecSock );
              while( pEvent != NULL )
              {
                // generate a structured reference if the event does not have a reference
                eventRef = pEvent->getRef();
                if( eventRef.empty() ) eventRef = pEvent->generateRef();

                if( pEvent->getType() == baseEvent::EV_COMMAND )
                {
                  // execute command requested - either commands or later responses to remote events
                  if( pEvent->getCommand( ) == baseEvent::CMD_SEND_UDP_PACKET )
                  {
                    sendUdpPacket( pEvent );
                  } // if
                  else if( pEvent->getCommand( ) == baseEvent::CMD_STATS ) 
                  {
                    std::string time = pEvent->getParam( "time" );
                    dumpHttp( time );
                  } // else if
                  else if( pEvent->getCommand( ) == baseEvent::CMD_RESET_STATS )
                  {
                    log.debug( log.MIDLEVEL, "main: baseEvent::CMD_RESET_STATS" );
                    theRecoveryLog->resetCountRecoveryLines( );
                  } // else if
                  else if( pEvent->getCommand( ) == baseEvent::CMD_REOPEN_LOG )
                  {
                    log.debug( log.MIDLEVEL, "main: baseEvent::CMD_REOPEN_LOG" );
                    log.reopenLogfile( );
                    theRecoveryLog->reOpen( );
                  } // if
                  else if( pEvent->getCommand( ) == baseEvent::CMD_REREAD_CONF )
                  {
                    // don't support it at the moment
                  } // if
                  else if( pEvent->getCommand( ) == baseEvent::CMD_SHUTDOWN )
                  {
                    log.info( log.LOGALWAYS, "main: baseEvent::CMD_SHUTDOWN" );
                    bRunning = false;
                  } // if
                  else
                    log.warn( log.LOGALWAYS, "main baseEvent failed to handle command %d", pEvent->getCommand( ) );

                  delete pEvent;
                } // if if( pEvent->getType() == baseEvent::EV_COMMAND
                else if( pEvent->getType() == baseEvent::EV_RESULT )
                {
                  // attempt to write the result back to the networkIf the request originated from
                  int returnFd = pEvent->getReturnFd();
                  if( returnFd > -1 )
                  {
                    void* refP = pEvent->getReturnFdRefAsVoidP();
                    // we should really make sure we are writing back to the same client again .. - could use the unixSocket object pointer as a rough key
                    std::map<int,tConnectData*>::iterator it;
                    it = tcpFds.find( returnFd );
                    if( it != tcpFds.end( ) )
                    {
                      tConnectData* pConnect = it->second;
                      if( refP == (void*)(pConnect->pSocket) )
                      {
                        if( log.wouldLog( log.LOGONOCCASION ) ) log.info() << "main wrote reply to fd: " << returnFd << " reply: " << pEvent->toString();
                        int ret = pEvent->serialiseNonBlock( returnFd );
                        if( ret == -1 )
                        {
                          log.warn( log.LOGNORMAL, "main failed on writing result back to fd:%s", returnFd );
                        } // if
                        else if( ret == 0 )
                        {
                          // we only had a part write - retrieve the unwritten fragment
                          // we are not making provision to not overwrite a previous fragment hanging around. there is currently in txProc
                          // no model to produce multiple return packets
                          pConnect->fragmentData = pEvent->getStrSerialised();
                          pConnect->bFragmentData = true;
                          rebuildPollList();
                        } // else if
                      } // else
                      else
                      {
                        log.warn() << log.LOGMOSTLY << "main fd: " << returnFd << " reference: " << refP << " not the same as unixSocket*: " << (void*)it->second;
                      } // else
                    } // if
                    else
                      log.warn( log.LOGMOSTLY, "main fd: %d no longer available to write response to", returnFd );
                  } // if
                  delete pEvent;
                } // if dispatchResultEvent
                else
                {
                  log.warn( log.LOGALWAYS ) << "unsupported event in main from fd:" << fd << " event: " << pEvent->toString();
                  delete pEvent;
                } // else

                pEvent = NULL;
                pEvent = baseEvent::unSerialise( pRecSock );
              } // while( pEvent != NULL
            } // if( fd == eventSourceFd
            else if( fd == listenTcpFd )
            {
              // accept the networkIf and add to the poll structure
              int newTcpFd = accept( listenTcpFd, NULL, NULL );
              if( newTcpFd > 0 )
              {
                if( addFdToPoll( newTcpFd ) )
                {
                  char name[64];
                  sprintf( name, "tcpFd-%d", newTcpFd );
                  unixSocket* pSock = new unixSocket( newTcpFd, unixSocket::ET_QUEUE_EVENT, false, name );
                  pSock->setNonblocking();
                  tConnectData* pConnect = new tConnectData;
                  pConnect->pSocket = pSock;
                  pConnect->bFragmentData = false;
                  tcpFds.insert( std::pair<int,tConnectData*>( newTcpFd, pConnect ) );
                  log.info( log.MIDLEVEL, "accepted new tcp fd %d", newTcpFd );
                  writeGreeting( pSock );
                } // if
                else
                {
                  close( newTcpFd );
                  log.error( "main: could not accept - closed fd %d", newTcpFd );
                } // else
              } // if
              else
                log.error( "accept returned error: ", strerror( errno ) );
            } // elseif( fd == listenTcpFd )
            else if( fd == listenUnFd )
            {
              if( UN_TYPE == SOCK_DGRAM )
              {
                dispatchPacket( fd, false );
              } // if UN_TYPE == SOCK_DGRAM
              else
              {
                // accept the networkIf and add to the poll structure
                int newUnFd = acceptUnSocket( );
                if( newUnFd > 0 )
                {
                  if( addFdToPoll( newUnFd ) )
                  {
                    unixSocket* pSock = new unixSocket( newUnFd, unixSocket::ET_QUEUE_EVENT );
                    pSock->setNonblocking( );
                    tConnectData* pConnect = new tConnectData;
                    pConnect->pSocket = pSock;
                    pConnect->bFragmentData = false;
                    tcpFds.insert( std::pair<int,tConnectData*>( newUnFd, pConnect ) );
                    log.info( log.MIDLEVEL, "accepted new unix fd %d", newUnFd );
                    writeGreeting( pSock );
                  } // if
                  else
                  {
                    close( newUnFd );
                    log.error( "main: could not accept 1 - closed fd %d", newUnFd );
                  } // else
                } // if
                else
                  log.error( "acceptUnSocket should have returned a valid fd not %d", newUnFd );
              } // else
            } // elseif( fd == listenUnFd )
            else if( fd == listenUdpFd )
            {
              dispatchPacket( fd, false );
            } // elseif( fd == listenUdpFd )
            else
            {
              dispatchPacket( fd, true );
            } // else( fd == eventSourceFd
          } // if eventType POLLIN
          else if( eventType & POLLOUT )
          {
            // check if we have any outstanding fragments
            std::map<int,tConnectData*>::iterator it;
            it = tcpFds.find( fd );
            if( it != tcpFds.end( ) )
            {
              tConnectData* pConnect = it->second;
              if( pConnect->bFragmentData )
              {
                // send the fragment
                int ret = unixSocket::writeOnce( fd, pConnect->fragmentData, pConnect->pSocket->getPipe() );
                if( ret < 1 )
                {
                  log.warn( log.LOGALWAYS, "main: failed to write fragmentData for fd:%d bytes:%d", fd, pConnect->fragmentData.length() );
                  closeAndRemoveFd( fd );
                } // error handling
                else if( ret < (int)pConnect->fragmentData.length() )
                {
                  // we again have a part write
                  pConnect->fragmentData = pConnect->fragmentData.substr( ret );       // dump the part that has been written
                  log.debug( log.LOGSELDOM, "main: wrote fragment 1 on fd:%d bytes:%d wrote:%d", fd, pConnect->fragmentData.length(), ret );
                } // else if
                else
                {
                  log.debug( log.LOGSELDOM, "main: wrote fragment on fd:%d bytes:%d wrote:%d", fd, pConnect->fragmentData.length(), ret );
                  pConnect->bFragmentData = false;
                  pConnect->fragmentData.clear();
                  rebuildPollList( );
                } // else
              } // if
            } // if
            else
            {
              log.warn( log.LOGALWAYS, "main: POLLOUT failed to locate fd:%d in tcpFds", fd );
            } // else
          } // else eventType POLLOUT
          else
          {
            log.warn( log.LOGALWAYS, "main: poll error on fd:%d", fd );
            closeAndRemoveFd( fd );
          } // else 
        } // while
      } // if( bReady
    } // try
    catch( Exception e )
    {
      log.error( "main: caught exception %s", e.getMessage() );
    } // catch
    catch( std::runtime_error e )
    {
      log.error( "main: caught std::runtime_error:'%s'", e.what() );
    } // catch
  } // while
  log.info( log.LOGALWAYS, "main: exiting" );
} // main

/**
 * sends a packet to the requested UDP address
 * @param pCommand - contains the request
 * **/
void networkIf::sendUdpPacket( baseEvent* pCommand )
{
  std::string address;    // ip address or box name
  std::string service;    // service name (/etc/services) or port number
  std::string payload;    // actual data to be sent (normally a serialised event)
  
  address = pCommand->getParam( "address" );
  service = pCommand->getParam( "service" );
  payload = pCommand->getParam( "payload" );

  if( (address.length()==0) || (service.length()==0) || (payload.length()==0) )
  {
    log.warn( log.LOGALWAYS ) << "sendUdpPacket not all parameters were provided - command: " << pCommand->toString();
    return;
  } // if
  
  // translate the address to number form 
  struct addrinfo hint;
  hint.ai_flags = 0;
  hint.ai_family = 0;
  hint.ai_socktype = SOCK_DGRAM;
  hint.ai_protocol = 0;
  hint.ai_addrlen = 0;
  hint.ai_canonname = NULL;
  hint.ai_addr = NULL;
  hint.ai_next = NULL;

  int err;
  struct addrinfo *ailist;
  struct addrinfo *aip;
  if( ( err = getaddrinfo( address.c_str(), service.c_str(), &hint, &ailist ) ) != 0 )
  {
    log.warn( log.LOGALWAYS ) << "sendUdpPacket getaddrinfo error: " << gai_strerror( err ) << " command: " << pCommand->toString();
    return;
  } // if

  int fd = -1;
  // if there are multiple address use the first one that works
  for( aip = ailist; aip != NULL; aip = aip->ai_next )
  {
    if( ( fd = ::socket( aip->ai_family, SOCK_DGRAM, 0  ) ) < 0 )
      err = errno;
    else
    {
      if( sendto( fd, payload.c_str(), payload.length(), 0, aip->ai_addr, aip->ai_addrlen ) < 0 )
      {
        log.warn( log.LOGALWAYS ) << "sendUdpPacket sendto error: " << strerror( errno ) << " command: " << pCommand->toString();
      } // if
      else
      {
        if( log.LOGONOCCASION < log.getLogLevel() )
          log.info( log.MIDLEVEL, "sendUdpPacket sent packet to host %s, service %s", address.c_str(), service.c_str() );
        else if( log.MIDLEVEL < log.getLogLevel() )
          log.warn( log.LOGALWAYS ) << "sendUdpPacket sent packet to host " << address << " service " << service << " command: " << pCommand->toString();
        break;
      } // else sendto
    } // else networkIf
  } // for
  
  // free the returned addresses and networkIf
  if( fd > -1 ) close( fd );
  freeaddrinfo( ailist );
} // sendUdpPacket

/**
 * reads and then dispatches a packet from a publicly available networkIf (udp or tcp)
 * closes a networkIf that returns 0 bytes
 * @param fd - to read the packet from
 * @param bWriteReply on networkIf - do not use for datagram networkIfs
 * **/
void networkIf::dispatchPacket( int fd, bool bWriteReply )
{
  baseEvent* pEvent = NULL;
  unixSocket* pSocket = NULL;
  try
  {
    std::map<int,tConnectData*>::iterator it;
    it = tcpFds.find( fd );
    if( it != tcpFds.end( ) )
    {
      pSocket = it->second->pSocket;
      pEvent = baseEvent::unSerialise( pSocket );
      while( pEvent != NULL )
      {
        // add reference and returnFd info if required
        bool bReplyRequested = false;
        if( bWriteReply )
        {
          int returnFd = pEvent->getReturnFd();
          if( returnFd == 0 )
          {
            pEvent->addReturnRouting( fd, (void*)pSocket );
            pEvent->addReturnRouting( fdSendSock, (const char*)NULL );
            bReplyRequested = true;
          } // if
          else if( returnFd > 0 )
          {
            // assume the existing routing info to be a valid networkIf process fd
            // add the sendSock fd to get the reply back to networkIf so that it can 
            // dispatch it
            pEvent->addReturnRouting( fdSendSock, (const char*)NULL );
          } // if
        } // if

        if( pEvent->getType() == baseEvent::EV_COMMAND )
        {
          if( log.wouldLog( log.LOGONOCCASION ) )
            log.debug( log.MIDLEVEL ) << "dispatching command event received on fd " << fd << " :" << pEvent->toString( );
          else if( log.wouldLog( log.MIDLEVEL ) )
            log.info( log.MIDLEVEL ) << "dispatching command event received on fd " << fd << " " << pEvent->commandToString();

          if( pEvent->getCommand() == baseEvent::CMD_NUCLEUS_CONF )
            pEvent->serialise( fdNucleusSock );  // send straight to the dispatcher
          else if( pEvent->getCommand() == baseEvent::CMD_NETWORKIF_CONF )
            reconfigure( pEvent );  // process directly
          else if( pEvent->getCommand()==baseEvent::CMD_MAIN_CONF )
            pEvent->serialise( fdParentSock );    // send to the main process
          else if( pEvent->getCommand( )==baseEvent::CMD_PERSISTENT_APP )
            pEvent->serialise( fdNucleusSock );
          else
          { // all other commands including CMD_APP get sent to the main process
            if( pEvent->getReadyTime() > 0 )
              pEvent->serialise( fdNucleusSock );
            else
              pEvent->serialise( fdParentSock );  // send to the main process
          } // else

          if( bWriteReply ) printResultToSocket( pSocket, true, bReplyRequested );
        } // if EV_COMMAND
        else if(  (pEvent->getType()==baseEvent::EV_URL) ||
            (pEvent->getType()==baseEvent::EV_SCRIPT) ||
            (pEvent->getType()==baseEvent::EV_PERL) ||
            (pEvent->getType()==baseEvent::EV_BIN) ||
            (pEvent->getType()==baseEvent::EV_RESULT)
            )
        {
          // send to the dispatcher
          if( log.wouldLog( log.LOGONOCCASION ) )
            log.debug( log.MIDLEVEL ) << "dispatching event received on fd " << fd << " : " << pEvent->toString( );
          else if( log.wouldLog( log.MIDLEVEL ) )
            log.info( log.MIDLEVEL ) << "dispatching event received on fd " << fd << " type: " << pEvent->typeToString();

          // send it on its way
          pEvent->serialise( fdNucleusSock );
          if( bWriteReply ) printResultToSocket( pSocket, true, bReplyRequested );
        } // if
        else
        {
          log.warn( log.LOGALWAYS ) << "main unable to handle event on fd " << fd << " : " << pEvent->toString( );
          if( bWriteReply ) printResultToSocket( pSocket, false );
        } // else

        delete pEvent;
        pEvent = NULL;
        pEvent = baseEvent::unSerialise( pSocket );
      } // while

      // an eof condition indicates that the networkIf is closed
      if( pSocket->isEof( ) )
      {
        close( fd );
        delete it->second->pSocket;
        delete it->second;
        tcpFds.erase( it );
        log.info( log.MIDLEVEL, "dispatchPacket closed fd %d", fd );
        rebuildPollList( );
      } // if
      pSocket = NULL;
    } // if
    else
    {
      log.error( "dispatchPacket: fd %d not found in tcpFds - closing networkIf" );
      close( fd );
      rebuildPollList( );
    } // else
  } // try
  catch( Exception e )
  {
    // already logged
    if( pEvent != NULL ) delete pEvent;
    pEvent = NULL;
    if( (pSocket!=NULL) && bWriteReply ) printResultToSocket( pSocket, false );
  } // catch
  catch( std::runtime_error e )
  { // json-cpp throws runtime_error
    if( pEvent != NULL )
    {
      log.error() << "dispatchPacket: caught std::runtime_error:'" << e.what() << "' event:" << pEvent->toString();
      delete pEvent;
    } // if
    else
      log.error( "dispatchPacket: caught std::runtime_error:'%s'", e.what() );
    pEvent = NULL;
    if( (pSocket!=NULL) && bWriteReply ) printResultToSocket( pSocket, false );
  } // catch
} // dispatchPacket

/**
 * write an initial greeting to an external stream networkIf connection
 * @param pSocket
 * **/
void networkIf::writeGreeting( unixSocket* pSocket )
{
  char str[1024];
  snprintf( str, 1024, "%03d:%s", (int)greetingString.length(), greetingString.c_str() );
  pSocket->write( str, strlen( str ) );
} // writeGreeting

/**
 * prints a success / fail result
 * @param pSocket
 * @param bSuccess - true if success result
 * **/
void networkIf::printResultToSocket( unixSocket* pSocket, bool bSuccess, bool bExpectReply )
{
  baseEvent replyPacket( baseEvent::EV_REPLY );
  replyPacket.setRef( eventRef );
  replyPacket.setSuccess( bSuccess );
  replyPacket.setExpectReply( bExpectReply );
  replyPacket.serialise( pSocket->getSocketFd() );

  log.debug( log.LOGONOCCASION, "printResultToSocket: fd:%d parsed:%s reply:%s", pSocket->getSocketFd(), bSuccess?"success":"failure", bExpectReply?"reply":"noreply" );
} // printResultToSocket

/**
 * handles a reconfigure command
 * **/
void networkIf::reconfigure( baseEvent* pCommand )
{
  try
  {
    log.info( log.LOGMOSTLY ) << "reconfigure: received command '" << pCommand->toString() << "'";
    std::string cmd = pCommand->getParam( "cmd" );
    if( (cmd.length()==0) )
      throw Exception( log, log.WARN, "reconfigure: require parameter cmd" );

    if( cmd.compare( "loglevel" ) == 0 )
    {
      std::string val = pCommand->getParam( "val" );
      if( val.length()==0 )
        throw Exception( log, log.WARN, "reconfigure: require parameter val for command '%s'", cmd.c_str() );

      int newLevel = atoi( val.c_str() );
      log.info( log.LOGALWAYS, "reconfigure: setting loglevel to %d", newLevel );
      pOptionsNetworkIf->defaultLogLevel = newLevel;
      log.setDefaultLevel( (loggerDefs::eLogLevel) pOptionsNetworkIf->defaultLogLevel );
    } // if( cmd.compare
    else
      log.warn( log.LOGALWAYS, "reconfigure: cmd '$s' not supported", cmd.c_str() );
  } // try
  catch( Exception e )
  {
  } // catch
} // reconfigure


/**
 * waits in blocking mode for a message / event from any of the networkIfs
 * does not restart after a signal
 * @return true if there are available fds for reading - use getNextFd() to retrieve
 * @exception on error
 * **/
bool networkIf::waitForEvent( )
{
  numPollFdsProcessed = 0;
  numPollFdsAvailable = 0;
  lastFdProcessed = -1;

  // wait infinite time (in blocking mode) for any events
  int retVal = 0;
  retVal = poll( pollFd, numPollFdEntries, -1 );
  if( retVal == -1 )
  {
    if( errno == EINTR )
    { 
      return false;  // we have been interrupted but no new messages
    }
    throw Exception( log, log.ERROR, "waitForEvent returned -1 error: %s", strerror( errno ) );
  } // if

  numPollFdsAvailable = retVal;
  log.debug( log.LOGSELDOM, "waitForEvent %d fds available", retVal );
  return true;
} // waitForEvent

/**
 * adds an fd to the pollFd structure
 * @param fd
 * @param bOutAsWell
 * **/
bool networkIf::addFdToPoll( int fd, bool bOutAsWell )
{
  if( numPollFdEntries < maxNumPollFdEntries )
  {
    if( numPollFdEntries == maxNumPollFdEntries-1 )
    {
      log.warn( log.LOGMOSTLY, "addFdToPoll: poll array full - no longer listening for new connections - numPollFdEntries:%d maxNumPollFdEntries:%d", numPollFdEntries, maxNumPollFdEntries );
      rebuildPollList( true ); // remove the listening networkIfs
    } // if

    pollFd[numPollFdEntries].fd = fd;
    pollFd[numPollFdEntries++].events = POLLIN | (bOutAsWell?POLLOUT:0);
    log.debug( log.LOGONOCCASION, "addFdToPoll: added fd:%d numPollFdEntries:%d maxNumPollFdEntries:%d", fd, numPollFdEntries, maxNumPollFdEntries );

    return true;
  } // if
  else
  {
    log.error( "addFdToPoll number of entries exceeded for fd %d" );
    return false;
  } // else
} // addFdToPoll

/**
 * rebuild poll list - typically used after deleting a fd
 * only listen for connections to accept if there is space
 * assumes the 1st 2(3) entries remain constant.  the Unix domain networkIf
 * if in stream mode should also be regarded as permanent. the udp networkIf and or 
 * Unix domain networkIf (if in SOCK_DGRAM mode) also remains constant but is 
 * inserted into the tcpFds map and is therefor added everytime by rebuildPollList
 * **/
void networkIf::rebuildPollList( bool bAdding )
{
  numPollFdEntries = 0;
  addFdToPoll( eventSourceFd );
  int numExistingStreamConnections = tcpFds.size()+numPollFdEntries;
  int maxEntriesToAllow = maxNumPollFdEntries-(bAdding?2:1);  // if we are adding the fd is not yet in the array

  // the stream listening networkIfs have to be added explicitly to the array given there is
  // sufficient space to accept new connections
  // the datagram listening networkIfs are already in tcpFds array
  // note that the unix domain networkIf behaves dependent on how it is configured
  if( numExistingStreamConnections < maxEntriesToAllow) 
  {
    addFdToPoll( listenTcpFd );   // should signal when there is a new networkIf to accept
    numExistingStreamConnections++;
  } // if
  if( (numExistingStreamConnections<maxEntriesToAllow) && (UN_TYPE!=SOCK_DGRAM) )
  {
    addFdToPoll( listenUnFd );   // should signal when there is a new networkIf to accept
    numExistingStreamConnections++;
  } // if

  std::map<int,tConnectData*>::iterator it;
  if( !tcpFds.empty() )
  {
    for( it = tcpFds.begin(); it != tcpFds.end(); it++ )
    {
      int fd = it->first;
      if( fd == listenUnFd )
      {
        if( UN_TYPE == SOCK_DGRAM ) // should only be in the array if it is configured as a datagram networkIf
          addFdToPoll( fd );
      } // if
      else
        addFdToPoll( fd, it->second->bFragmentData );
    } // if
  } // if
} // rebuildPollList

/**
 * returns the next available file descriptor or -1 if none
 * **/
int networkIf::getNextFd( short& eventType )
{
  log.debug( log.LOGSELDOM, "getNextFd numPollFdsProcessed %d numPollFdsAvailable %d lastFdProcessed %d", numPollFdsProcessed, numPollFdsAvailable, lastFdProcessed );
  if( numPollFdsProcessed == numPollFdsAvailable ) return -1;
  bool bFound = false;
  while( !bFound && (++lastFdProcessed < numPollFdEntries ) )
  {
    eventType = pollFd[lastFdProcessed].revents;
    if( pollFd[lastFdProcessed].revents & (POLLIN | POLLOUT | POLLPRI) )
    {
      bFound = true;
      numPollFdsProcessed++;
      return pollFd[lastFdProcessed].fd;
    }
    else if( pollFd[lastFdProcessed].revents & (POLLERR | POLLHUP | POLLNVAL) )
    {
      // some form of error has occurred - remove the fd and close it
      int fd = pollFd[lastFdProcessed].fd;
      log.error( "getNextFd: poll returned error on fd %d", fd );
      closeAndRemoveFd( fd );
    } // else
  } // while
  return -1;
} // getNextFd

/**
 * close and remove an fd
 * @param fd
 * **/
void networkIf::closeAndRemoveFd( int fd )
{
  log.info( log.MIDLEVEL, "closeAndRemoveFd fd %d", fd );
  if( fd < 0 ) return;
  
  std::map<int,tConnectData*>::iterator it;
  it = tcpFds.find( fd );
  if( it != tcpFds.end( ) )
  {
    delete it->second->pSocket;
    delete it->second;
    tcpFds.erase( it );
  } // if
  else
    log.error( "closeAndRemoveFd: could not find fd %d in tcpFds", fd );
  close( fd );
  rebuildPollList( );
} // closeAndRemoveFd

/**
 * adapted from Stevens and Rago
 * retrieves address info for a networkIf to listen on - inits a server
 * networkIf to listen
 * @param type SOCK_STREAM, SOCK_DGRAM
 * @param service - entry in /etc/services
 * @param listenAddr - address to listen on - can be INADDR_ANY,hostname,hostIp or empty in which case hostname will be used
 * @return the file descriptor for the initialised networkIf
 * @exception on error
 * **/
int networkIf::createListenSocket( int type, const std::string& service, std::string& listenAddr )
{
  int err;
  int serverFd = -1;
  struct addrinfo *ailist;
  struct addrinfo *aip;
  const char* node;

  if( listenAddr.length() == 0 ) listenAddr = hostId;
  if( listenAddr.compare("INADDR_ANY") == 0 )
    node = NULL;
  else
    node = listenAddr.c_str();
  log.info( log.LOGMOSTLY, "createListenSocket host %s, service %s, type %s", listenAddr.c_str(), service.c_str(), (type == SOCK_STREAM)?"SOCK_STREAM":"SOCK_DGRAM" );

  // lookup the address info
  struct addrinfo hint;
  hint.ai_flags = AI_CANONNAME | (node==NULL)?AI_PASSIVE:0;
  hint.ai_family = AF_INET;
  hint.ai_socktype = type;
  hint.ai_protocol = 0;
  hint.ai_addrlen = 0;
  hint.ai_canonname = NULL;
  hint.ai_addr = NULL;
  hint.ai_next = NULL;
  if( ( err = getaddrinfo( node, service.c_str(), &hint, &ailist ) ) != 0 )
    throw Exception( log, log.ERROR, "getaddrinfo error: %s", gai_strerror(err) );

  for( aip = ailist; aip != NULL; aip = aip->ai_next )
  {
    if( ( serverFd = initServer( type, aip->ai_addr, aip->ai_addrlen ) ) >= 0 )
      break;
  } // for
  if( serverFd <= 0 ) log.warn( log.LOGALWAYS, "createListenSocket failed to find a valid service/address to listen on for type %d service %s", type, service.c_str() );

  // free the returned addresses
  freeaddrinfo( ailist );

  if( serverFd >= 0 ) log.info( log.LOGMOSTLY, "createListenSocket done fd %d", serverFd );
  return serverFd;
} // createListenSocket

/**
 * create an Unix domain networkIf for listening on
 * networkIf to listen
 * adjust the ownership / access permissions
 * @param type SOCK_STREAM, SOCK_DGRAM
 * @param networkIfPath
 * @param Qlen
 * @return the file descriptor for the initialised networkIf
 * @exception on error
 * **/
int networkIf::createUnListenSocket( int type, const char* networkIfPath, int qlen )
{
  int fd;
  struct sockaddr_un un;

  if( ( unlink( networkIfPath ) < 0 ) && ( errno != ENOENT ) )
    log.error( "unlink of %s failed: %s", networkIfPath, strerror( errno ) );

  un.sun_family = AF_UNIX;
  strcpy( un.sun_path, networkIfPath );
  if( ( fd = ::socket( AF_UNIX, type, 0 ) ) < 0 )
    throw Exception( log, log.ERROR, "createUnListenSocket networkIf error: %s", strerror( errno ) );

  int size = offsetof( struct sockaddr_un, sun_path ) + strlen( un.sun_path );
  if( bind( fd, (struct sockaddr *)&un, size ) < 0 )
    log.error( "init failed to bind address %s to unix networkIf %d: %s", networkIfPath, fd, strerror( errno ) );
  else
    log.info( log.LOGMOSTLY, "bound unix domain networkIf %d to address %s", fd, networkIfPath );

  if( type == SOCK_STREAM )
  {
    if( listen( fd, qlen ) < 0 )
      throw Exception( log, log.ERROR, "createUnListenSocket listen error: %s", strerror( errno ) );
  } // if

  // adjust the ownership / access permissions to be that of the user / group we are running as
  if( chmod( un.sun_path, S_IRUSR | S_IWUSR | S_IXUSR | S_IRGRP | S_IWGRP | S_IXGRP ) < 0 )
    log.warn( log.LOGALWAYS, "createUnListenSocket chmod failed: %s", strerror( errno ) );
  
  struct passwd* pw = getpwnam( pOptionsNetworkIf->runAsUser.c_str() );
  if( pw == NULL ) 
  {
    log.error( "createUnListenSocket getpwnam could not find user %s", pOptionsNetworkIf->runAsUser.c_str() );
  } // if
  else
  {
    int userId = pw->pw_uid;
    int groupId = pw->pw_gid; // use this as a default fallback
    struct group* gr = getgrnam( pOptionsNetworkIf->socketGroup.c_str() );
    if( gr == NULL )
      log.error( "createUnListenSocket getgrnam could not find group %s using default %d", pOptionsNetworkIf->socketGroup.c_str(), groupId );
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
 * adapted from Stevens and Rago
 * accepts a new connection on the listening Unix domain networkIf
 * Obtain the client's user ID from the pathname that it must bind to before
 * calling us
 * @return the new fd
 * @exception on error
 * **/
int networkIf::acceptUnSocket( )
{
  int fd;
  socklen_t len;
  struct sockaddr_un  un;
  
  len = sizeof( un );
  log.debug( log.LOGNORMAL, "acceptUnSocket struct len %d", len );
  while( ( ( fd = accept( listenUnFd, (struct sockaddr *)&un, &len ) ) < 0 ) && ( errno == EINTR ) );
  if( fd < 0 )
    throw Exception( log, log.ERROR, "acceptUnSocket accept error: %s", strerror( errno ) );

  // for some or other reason we don't receive the calling party's address
  // obtain the client's uid from its calling address
  // this seems to be the result of the system by default using the abstract namespace
  // and/or not creating a named local (client perspective) fifo for the connection.
  // explicitly create a networkIf in perl with "Local   => '/tmp/12345g.sock'" to get this
  // code to work
#if 0
  log.debug( "acceptUnSocket new fd %d, struct len %d", fd, len );
  struct stat         statbuf;
  len -= offsetof( struct sockaddr_un, sun_path );    // length of the pathname
  un.sun_path[len] = '\0';
  if( stat( un.sun_path, &statbuf ) < 0 )
    throw Exception( log, log4cpp::Priority::ERROR, "acceptUnSocket stat error: %s on %s", strerror( errno ), un.sun_path );
  if( S_ISSOCK( statbuf.st_mode ) == 0 )
    throw Exception( log, log4cpp::Priority::ERROR, "acceptUnSocket stat error: %s is not a networkIf", un.sun_path );
  if( ( statbuf.st_mode & (S_IRWXG | S_IRWXO ) ) || ( statbuf.st_mode & S_IRWXU ) != S_IRWXU )
    throw Exception( log, log4cpp::Priority::ERROR, "acceptUnSocket stat error: %s is not rwx", un.sun_path );
  
  int staletime = time( NULL ) - STALE;
  if( statbuf.st_atime < staletime || statbuf.st_ctime < staletime || statbuf.st_mtime < staletime )
    throw Exception( log, log4cpp::Priority::ERROR, "acceptUnSocket stat error: %s is too old", un.sun_path );
    
//  int uid  = statbuf.st_uid;  // if ever we are interested
  unlink( un.sun_path );      // done with this path
#endif
  return fd;
} // acceptUnSocket

/**
 * adapted from Stevens and Rago
 * initialises a server networkIf
 * @param type SOCK_STREAM, SOCK_DGRAM
 * @param addr the address to bind to (sockaddr*)
 * @param alen length of the sockaddr structure
 * @param qlen - backlog - has a default value
 * @exception on error
 * @return new file descriptor or -1 failure to bind
 * **/
int networkIf::initServer( int type, const struct sockaddr *addr, socklen_t alen, int qlen )
{
  int fd;
  int reuse = 1;

  if( log.MIDLEVEL < log.getLogLevel() )
  {
    const char* strType = (type == SOCK_STREAM)?"SOCK_STREAM":"SOCK_DGRAM";
    const char* strFamily;
    switch( addr->sa_family )
    {
      case AF_INET: strFamily = "AF_INET"; break;
      case AF_INET6: strFamily = "AF_INET6"; break;
      default:
        strFamily = "family other";
    } // switch
    char ipAddr[INET6_ADDRSTRLEN];
    sockaddr_in* addrP = (struct sockaddr_in*)addr;
    ipAddr[0] = '\0';
    if( inet_ntop( addr->sa_family, (const void *)&addrP->sin_addr, ipAddr, INET6_ADDRSTRLEN ) )
    {
      log.info( log.LOGMOSTLY, "initServer trying family %s type %s address %s port %d", strFamily, strType, ipAddr, ntohs( addrP->sin_port ) );
    }
    else
      log.error( "initServer failed to convert the ip address - error: %s", strerror( errno ) );
  } // if
        
  if( ( fd = ::socket( addr->sa_family, type, 0 ) ) < 0 )
    throw Exception( log, log.ERROR, "networkIf returned -1 error: %s", strerror( errno ) );
  try
  {
    if( setsockopt( fd, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof( int ) ) < 0 )
      throw Exception( log, log.ERROR, "setsockopt returned -1 error: %s", strerror( errno ) );
    if( bind( fd, addr, alen ) < 0 )
    {
      log.warn( log.LOGALWAYS, "bind returned -1 - error %s", strerror( errno ) );
      close( fd );
      return -1;
    }
    log.debug( log.LOGNORMAL, "initServer bound" );
    if( type == SOCK_STREAM || type == SOCK_SEQPACKET )
    {
      if( listen( fd, qlen ) < 0 )
        throw Exception( log, log.ERROR, "listen returned -1 - error %s", strerror( errno ) );
    } // if
    log.debug( log.LOGNORMAL, "initServer done fd %d", fd );
    return fd;
  } // try
  catch( Exception e )
  {
    close( fd );
    throw;
  } // catch
} // initServer

/**
 * drops priviledges from root to the user and the users default group
 * @param user
 * @return true on success
 * **/
bool networkIf::dropPriviledge( const char* user )
{
  if( getuid() != 0 )
  {
    log.warn( log.LOGALWAYS, "dropPriviledge: not running as root, cannot drop priviledge" );
    return true;
  } // if

  struct passwd* pw = getpwnam( user );
  if( pw == NULL ) 
  {
    log.error( "dropPriviledge could not find user %s", user );
    return false;
  }
  int userId = pw->pw_uid;
  int groupId = pw->pw_gid;
  log.info( log.LOGALWAYS, "dropPriviledge user %s uid %d gid %d pid %d", user, userId, groupId, getpid() );
  
  int result = setgid( groupId );
  if( result == -1 )
  {
    log.error( "dropPriviledge failed on setgid - %s", strerror( errno ) );
    return false;
  }
  result = setuid( userId );
  if( result == -1 )
  {
    log.error( "dropPriviledge failed on setuid - %s", strerror( errno ) );
    return false;
  }

  // set the process to be dumpable
#ifndef PLATFORM_MAC 
  if( prctl( PR_SET_DUMPABLE, 1, 0, 0, 0 ) == -1 )
    log.error( "dropPriviledge: prctl( PR_SET_DUMPABLE ) error - %s", strerror( errno ) );
#endif

  return true;
} // dropPriviledge

/**
 * dumps the stats - none for the moment for networkIf
 * **/
void networkIf::dumpHttp( const std::string& time )
{
} // dumpHttp

/**
 * signal handler
 * **/
void networkIf::sigHandler( int signo )
{
  switch( signo )
  {
    case SIGINT:
    case SIGTERM:
      bRunning = false;
      break;
    default:
      fprintf( stderr, "sigHandler cannot handle signal %s", strsignal( signo ) );
  } // switch
} // sigHandler

