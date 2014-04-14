/* 
 Represents the base class for a generic channel
 
 $Id: channel.h 1 2009-08-17 12:36:47Z gerhardus $
 Versioning: a.b.c  a is a major release, b represents changes or new additions, c is a bug fix
 @Version 1.0.0   15/04/2004    Gert Muller       Script created
 @version 2.0.0   14/05/2009    Gerhardus Muller  Adapted for use in the txDelta framework
 
 @note
 
 @todo
 
 @bug
 
 Copyright notice
 */

#ifndef __class_channel_has_been_included__
#define __class_channel_has_been_included__

#include "utils/object.h"

typedef void (*rxCallbackFn)( int channelId, char recChar);
typedef void (*txCallbackFn)( int callbackParam);

/** @class rxMessCallbackIF
 Represents callback funtionality support on a channel
 */
class rxMessCallbackIF
{
  public:
    virtual ~rxMessCallbackIF() {};
    virtual void rxMessCallback( int channelId, char* recBuf, int numChars ) = 0;
}; // class rxMessCallbackIF

class channel : public object
{
  // declarations

  // methods
  public:
    channel( const char* theType );
    virtual ~channel();
    virtual std::string toString( );

    /// returns the channel type
    virtual const char* getchannelType(){return channelType;};
    /// sets the channelID used for callbacks
    void setchannelId( int newID ){ channelID = newID; };
    /// returns the channelID
    int getchannelId( ){ return channelID; };
    /// returns the read file descriptor associated with the channel
    int getReadFileDescriptor( ){ return readFileDescriptor; };
    /// set the manager ID
    void setManagerId( int newID ){ managerID = newID; };
    /// gets the manager ID
    int getManagerId( ){ return managerID; };

    /** @name channel Interface
      Methods to access a channel by excluding the init method
      */
    //@{

    /** 
      Initialise from the given ini file object with the given section name.
      @param iniFile - ini file object to use
      @param section - section to use
      @exception Exception on failure to properly initialise
      */
//    virtual void init( readIni* iniFile, const std::string& section ) = 0;

    /**
      The write function is the entry point to write data packets using the particular channel
      Register a callback with registerTxDoneCallback to receive a notification of transmission complete
      @param buffer - buffer containing the data to be transmitted
      @param bufLen - number of bytes to be transmitted
      @return - the actual number of bytes that have been transmitted if relevant
      */
    virtual int write( const char* buffer, int bufLen ) = 0;

    /**
      Receives a data packet
      Should invoke the character received callback if defined
      @param buffer - buffer containing the received data
      @param bufLen - length of buffer
      @return - the number of bytes that have been received
      */
    virtual int receive( char* buffer, int bufLen ) = 0;

    /**
      If a callback is registered the function will be called for every character received on the channel
      @param theRxCallback - function to be called for every byte received
      */
    virtual void registerRxCharCallback( rxCallbackFn theRxCallback ){ rxCharCallback = theRxCallback; };

    /**
      If a callback is registered the function will be called for message (block) received on the channel
      @param theRxCallback - function to be called for every byte received
      */
    virtual void registerRxMessCallback( rxMessCallbackIF* theRxCallback ){ rxMessCallback = theRxCallback; };

    /**
      If a callback is registered the function will be called after transmission on the channel has completed.
      For serial channels this is typically when the last byte is written to the driver and for 
      TCP / UDP immediately unless the socket is blocking
      @param theTxCallback - function to call after tx has completed
      */
    virtual void registerTxDoneCallback( txCallbackFn theTxCallback ){ txDoneCallback = theTxCallback; };

    //@}


  protected:

  private:

    // properties
  public:

  protected:
    int               channelID;                  ///< Contains a user assigned channelID
    int               managerID;                  ///< channelManager generated ID
    const char*       channelType;                ///< channel type - to be overridden by base classes
    int               readFileDescriptor;         ///< Operating system file descriptor - used by the manager to wait on the channel for reading
    rxCallbackFn      rxCharCallback;             ///< If registered will be called for every character received on the channel
    rxMessCallbackIF* rxMessCallback;             ///< If registered will be called for every message (block) received on the channel
    txCallbackFn      txDoneCallback;             ///< If registered will be called at completion of the transmission

  private:

};  // class channel

#endif  // #ifndef __class_channel_has_been_included__
