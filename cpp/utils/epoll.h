/** 
 Utility class implementing epoll
 
 $Id: epoll.h 2588 2012-09-17 16:45:27Z gerhardus $
 Versioning: a.b.c a is a major release, b represents changes or new features, c represents bug fixes. 
 @version 1.0.0		28/01/2011		Gerhardus Muller		Script created
 @version 1.0.1		17/09/2012		Gerhardus Muller    events in getReadyRef should be an unsigned it

 @note

 @todo
 
 @bug

	Copyright Notice
 */

#if !defined( epoll_defined_ )
#define epoll_defined_

#include "utils/object.h"
#include <sys/epoll.h>

class epoll : public object
{
  // Definitions
  public:

    // Methods
  public:
    epoll( int theMaxEvents );
    virtual ~epoll();
    virtual std::string toString();
    void  addFd( int fd, void* ref, unsigned int type=EPOLLIN );
    void  deleteFd( int fd );
    int   waitForEvent( int timeout );
    int   getNextFd( );
    int   getNumReadyEvents( )                          {return numReadyEvents;}
    bool  isFdError( )                                  {return lastErrorFd != -1;}
    int   getLastErrorFd( )                             {return lastErrorFd;}
    void* getReadyRef( int index, bool& bErr, unsigned int& events ){if(index<numReadyEvents){events=pEvents[index].events;bErr=(events&(EPOLLERR|EPOLLHUP));return pEvents[index].data.ptr;}else throw Exception( log, log.ERROR, "getReadyRef: index out of range i:%d numReadyEvents:%d",index,numReadyEvents );}
    bool  getReadyError( int index );

  private:

    // Properties
  public:

  protected:

  private:
    int                         epollFd;              ///< handle for the epoll instance in the kernel
    int                         maxEvents;            ///< max events to be reported in a wait
    int                         numReadyEvents;       ///< number of ready events in pEvents
    int                         numFdsProcessed;      ///< number of entries processed - <= numPollFdsAvailable
    int                         lastFdProcessed;      ///< last fd that was processed
    int                         lastErrorFd;          ///< last fd with a detected error on it
    unsigned int                lastEvent;            ///< the events field of the last event returned by getNextFd
    struct epoll_event*         pEvents;              ///< holds the events after a wait
};	// class epoll

#endif // !defined( epoll_defined_)
