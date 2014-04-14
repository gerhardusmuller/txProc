/** @class epoll
 utility class implementing epoll 
 
 $Id: epoll.cpp 2629 2012-10-19 16:52:17Z gerhardus $
 Versioning: a.b.c a is a major release, b represents changes or new features, c represents bug fixes. 
 @version 1.0.0		28/01/2011		Gerhardus Muller		Script created
 @version 1.0.1		10/10/2012		Gerhardus Muller		waitForEvent not to throw if an EINTR occurs

 @note
waitForEvent
 @todo
 
 @bug

	Copyright Notice
 */

#include <errno.h>
#include <string.h>
#include <unistd.h>
#include "utils/epoll.h"

/**
 * construction
 * **/
epoll::epoll( int theMaxEvents )
  : object( "epoll" ),
    maxEvents( theMaxEvents )
{
  pEvents = NULL;
  numReadyEvents = 0;
  numFdsProcessed = 0;
  lastFdProcessed = -1;
  lastErrorFd = -1;
  epollFd = -1;
  epollFd = epoll_create( 10 );       // the 10 is arbitrary - no longer used
  if( epollFd == -1 ) throw Exception( log, log.ERROR, "epoll_create error: %s", strerror(errno) );
  pEvents = new struct epoll_event[maxEvents];
}	// epoll

/**
 * destruction
 * **/
epoll::~epoll()
{
  if( epollFd != -1 ) close( epollFd );
  if( pEvents != NULL ) delete[] pEvents;
}	// ~epoll

/**
 Standard logging call - produces a generic text version of the epoll.
 @return pointer to a string describing the state of the epoll.  The string has an unspecified lifetime and is not multi-thread safe
 */
std::string epoll::toString( )
{
  std::ostringstream oss;
	return oss.str();
}	// toString

/**
 * adds a file descriptor to the set
 * typedef union epoll_data
 * {
 *   void *ptr;
 *   int fd;
 *   uint32_t u32;
 *   uint64_t u64;
 * } epoll_data_t;
 * 
 * struct epoll_event
 * {
 *   uint32_t events;	// Epoll events
 *   epoll_data_t data;	// User data variable
 * } __attribute__ ((__packed__));
 * 
 * @param fd - file descriptor to add
 * @param ref - will be returned by getNextFd
 * @param type bit set: EPOLLIN (default),EPOLLOUT,EPOLLHUP
 * @exception on failure
 * **/
void epoll::addFd( int fd, void* ref, unsigned int type )
{
  struct epoll_event ev;
  ev.events = type;
  ev.data.ptr = ref;
  type |= EPOLLRDHUP | EPOLLERR | EPOLLHUP | EPOLLHUP;
  if( epoll_ctl( epollFd, EPOLL_CTL_ADD, fd, &ev ) == -1 )
    throw Exception( log, log.ERROR, "addFd: epoll_ctl fd:%d error:%s", fd, strerror(errno) );
  log.info( log.LOGNORMAL, "addFd: added fd:%d ref:%p", fd, ref );
} // addFd

/**
 * deletes a file descriptor from the set
 * @param fd - file descriptor to delete
 * @exception on failure
 * **/
void epoll::deleteFd( int fd )
{
  if( epoll_ctl( epollFd, EPOLL_CTL_DEL, fd, NULL ) == -1 )
    throw Exception( log, log.ERROR, "deleteFd: epoll_ctl fd:%d error:%s", fd, strerror(errno) );
  log.info( log.LOGNORMAL, "deleteFd: deleted fd:%d", fd );
} // deleteFd

/**
 * waits / checks for file handles that are ready
 * @param timeout in ms - -1 for indefinite
 * @return number of events ready or -1 when an EINTR has occurred
 * @exception on error
 * **/
int epoll::waitForEvent( int timeout )
{
  numFdsProcessed = 0;
  lastFdProcessed = -1;
  lastErrorFd = -1;
  numReadyEvents = epoll_wait( epollFd, pEvents, maxEvents, timeout );
  if( (numReadyEvents==-1) && (errno!=EINTR) ) throw Exception( log, log.ERROR, "waitForEvent:epoll_wait error: %s", strerror(errno) );
  return numReadyEvents;
} // waitForEvent

/**
 * this function can only be used if the reference passed in was the file descriptor
 * or an integer reference for which -1 is not a valid value
 * the calling program should check isFdError() and if error then close the fd and remove it using deleteFd
 * @return the reference for the next available file descriptor or -1 if none
 * **/
int epoll::getNextFd( )
{
  log.debug( log.LOGONOCCASION, "getNextFd: numFdsProcessed:%d numReadyEvents:%d lastFdProcessed:%d", numFdsProcessed, numReadyEvents, lastFdProcessed );
  if( numFdsProcessed == numReadyEvents ) return -1;

  int foundFdRef = int((unsigned long)pEvents[numFdsProcessed].data.ptr);
  numFdsProcessed++;
  lastEvent = pEvents[numFdsProcessed].events;
  log.debug( log.LOGONOCCASION, "getNextFd: lastEvent:0x%x", lastEvent );

  if( lastEvent & (EPOLLIN|EPOLLOUT) )
    return foundFdRef;

  if( lastEvent & (EPOLLERR|EPOLLHUP|EPOLLRDHUP) )
  {
    lastErrorFd = foundFdRef;
    if( lastEvent & EPOLLERR )
      log.warn( log.LOGALWAYS, "getNextFd: EPOLLERR on ref %d", lastErrorFd );
    else if( lastEvent & EPOLLHUP )
      log.warn( log.LOGALWAYS, "getNextFd: EPOLLHUP on ref %d", lastErrorFd );
    else if( lastEvent & EPOLLRDHUP )
      log.warn( log.LOGALWAYS, "getNextFd: EPOLLRDHUP on ref %d", lastErrorFd );
  } // if
  return foundFdRef;
} // getNextFd

/**
 * returns true if the requested ready fd has an error condition
 * the calling program should close the fd and remove it using deleteFd
 * @param index
 * @return true if error, false if not
 * **/
bool epoll::getReadyError( int index )
{
  if( index >= numReadyEvents ) throw Exception( log, log.ERROR, "getReadyError: index out of range i:%d numReadyEvents:%d", index, numReadyEvents );

  lastEvent = pEvents[index].events;
  if( lastEvent & (EPOLLERR|EPOLLHUP|EPOLLRDHUP) )
  {
    void *ref = pEvents[index].data.ptr;
    if( lastEvent & EPOLLERR )
      log.warn( log.LOGALWAYS, "getNextFd: EPOLLERR on ref %p", ref );
    else if( lastEvent & EPOLLHUP )
      log.warn( log.LOGALWAYS, "getNextFd: EPOLLHUP on ref %p", ref );
    else if( lastEvent & EPOLLRDHUP )
      log.warn( log.LOGALWAYS, "getNextFd: EPOLLRDHUP on ref %p", ref );
    return true;
  } // if
  else
    return false;
} // getReadyError
