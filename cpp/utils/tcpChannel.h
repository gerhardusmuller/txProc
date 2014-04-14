/* 
 Implementation of a TCP channel
 
 $Id: tcpChannel.h 1 2009-08-17 12:36:47Z gerhardus $
 Versioning: a.b.c  a is a major release, b represents changes or new additions, c is a bug fix
 @Version 1.0.0   31/05/2004    Gert Muller         Script created
 @version 2.0.0   14/05/2009    Gerhardus Muller    Adapted for use in the txDelta framework
 @version 2.1.0		11/06/2009		Gerhardus Muller		read throws on socket eof condition
 
 */

#ifndef __class_tcpChannel_has_been_included__
#define __class_tcpChannel_has_been_included__

#include "utils/channel.h"
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h> 
#include <arpa/inet.h>
#include <netdb.h>
#include <string.h>

class tcpChannel : public channel
{
  public:
    // declarations
    static const int DEF_BUF_LEN = 512;

    // methods
  public:
    tcpChannel();
    ~tcpChannel();
    void test( );
    /// Returns the remote port
    std::string& getRemotePort( )             {return remotePort;};
    /// Returns the remote IP
    std::string& getRemoteIP( )               {return remoteIPaddress;};
    /// Returns the file descriptor
    int getFd( )                              {return remoteSocket;}
    /// true if connected
    bool isConnected( )                       {return bConnected;}
    std::string& getLastError( )              {return lastError;}
    virtual std::string toString( );
    void allocLocalBuffer( int length )       {if(localBuffer!=NULL)delete[] localBuffer;localBufferLen=length;localBuffer=new char[length];}
    void setnonblocking( int sock );
    void setThrowEof( )                       {bThrowEof=true;}

    /** @name Channel Interface
      Methods to access a channel by excluding the init method
      */
    //@{
    void init( bool isServer, const std::string& ip, const std::string& port );
//    virtual void init( readIni* iniFile, const std::string& section );

    /**
      The write function is the entry point to write data packets using the particular channel
      Register a callback with registerTxDoneCallback to receive a notification of transmission complete
      @param buffer - buffer containing the data to be transmitted
      @param bufLen - number of bytes to be transmitted
      @return - the actual number of bytes that have been transmitted if relevant
      */
    virtual int write( const char* buffer, int bufLen );
    int write( const std::string& mess );

    /**
      Receives a TCP packet
      @param buffer - buffer for the received packet
      @param bufLen - length of the buffer
      @return - the number of bytes that have been received
      */
    virtual int receive( char* buffer, int bufLen );
    std::string& receive( );
    //@}


  protected:

  private:
    void init( );
    void createAddress( struct sockaddr_in &addr, const char* ip, int port );
    void lookupAddress( const char* ip, const char* port );
    int  createTCPSocket( );
    void bindTCPSocket( int fd );
    void acceptConnection( );
    void connect( );
    void close( );

    // properties
  public:

  protected:

  private:
    std::string         remoteIPaddress;            ///< remote target address
    std::string         remotePort;                 ///< port on which to listen
    std::string         lastError;                  ///< last socket error encountered
    std::string         readBuf;                    ///< buffer that contains the last message read with receive()
    int                 remoteSocket;               ///< socket (fd) on which we send
    int                 listenSocket;               ///< server socket that we are listening on
    bool                bBlocking;                  ///< true if the socket is opened in blocking mode
    bool                bServer;                    ///< true if it is a server socket
    bool                bAccepted;                  ///< only relevant for a server
    bool                bConnected;                 ///< only relevant for a client
    bool                bThrowEof;                  ///< if true throw on read on eof
    struct addrinfo*    ailist;                     ///< results of getaddrinfo
    char*               localBuffer;                ///< local buffer that clients can use
    int                 localBufferLen;             ///< length of this buffer
};  // tcpChannel

#endif  // #ifndef __class_tcpChannel_has_been_included__
