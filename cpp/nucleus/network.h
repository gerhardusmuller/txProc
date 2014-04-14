/**
 Network class eventually to replace in part networkIf.*
 
 $Id: network.h 2588 2012-09-17 16:45:27Z gerhardus $
 Versioning: a.b.c a is a major release, b represents changes or new features, c represents bug fixes. 
 @version 1.0.0		04/09/2012		Gerhardus Muller		Script created

 @note

 @todo
 
 @bug

	Copyright Notice
 * **/

#if !defined( network_defined_ )
#define network_defined_

#include "utils/object.h"
#include "utils/unixSocket.h"
#include "nucleus/baseEvent.h"
#include "utils/epoll.h"

class epoll;

typedef std::map<int,unixSocket*> socketMapT;
typedef socketMapT::iterator socketMapIteratorT;

class network : public object
{
  // Definitions
  public:

    // Methods
  public:
    network( );
    virtual ~network();
    int  getUnFd()                                            {return listenUnFd;}
    int  getUnStreamFd()                                      {return listenUnStreamFd;}
    int  waitForRdEvent( int timeout=-1 )                     {return pEpollRd->waitForEvent(timeout);}
    void resetRdPollMap();
    void resetWrPollMap();
    void buildRdPollMap();
    void addRdFd( unixSocket* pSock );
    void addWrFd( unixSocket* pSock );
    void deleteRdFd( unixSocket* pSock )                      {if(pSock!=NULL)deleteRdFd(pSock->getSocketFd());}
    void deleteWrFd( unixSocket* pSock )                      {if(pSock!=NULL)deleteWrFd(pSock->getSocketFd());}
    void deleteRdFd( int fd );
    void deleteWrFd( int fd );
    void closeStreamSocket( unixSocket* pSock )               {if(pSock!=NULL)closeStreamSocket(pSock->getSocketFd());}
    void closeStreamSocket( int fd );
    unixSocket* getReadSocket( int i, bool& bErr, unsigned int& events )  {return static_cast<unixSocket*>(pEpollRd->getReadyRef(i,bErr,events));}
    void listenEvent();

  private:
    void init();
    int createUnListenSocket( int type, const char* networkIfPath, int qlen=10 );
    int acceptUnSocket();
    void writeGreeting( unixSocket* pSocket );

  protected:

    // Properties  
  public:

  protected:
    int                               listenUnFd;                 ///< listen networkIf for Unix domain networkIf connections
    int                               listenUnStreamFd;           ///< listen networkIf for Unix domain networkIf connections - stream socket
    unixSocket*                       pUnSock;                    ///< corresponding socket object
    unixSocket*                       pUnStreamSock;              ///< corresponding socket object

  private:
    epoll*                            pEpollRd;                   ///< fd poll object for reading - replaces pRecSock's roll of providing a poll interface
    epoll*                            pEpollWr;                   ///< fd poll object for writing
    socketMapT                        tcpFds;                     ///< map containing the tcp/ud stream sockets we are serving
    std::string                       hostId;                     ///< hostname entry
    std::string                       greetingString;             ///< greeting string for external networkIf connections
};	// class network

#endif // !defined( network_defined_)
