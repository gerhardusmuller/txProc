/** @class channel
 Represents the base class for a generic channel for GTI.  Derived classes will support 
 TCP client / server, UDP and serial.  Only the init method is not contained in the base
 class - it is very dependent on the exact channel.
 
 $Id: channel.cpp 1 2009-08-17 12:36:47Z gerhardus $
 Versioning: a.b.c  a is a major release, b represents changes or new additions, c is a bug fix
 @version 1.0.0   15/04/2004    Gert Muller     Script created
 @version 1.1.0   15/10/2004    Gert Muller     Made provision for message callbacks in addition to character callbacks
 @version 2.0.0   14/05/2009    Gerhardus Muller  Adapted for use in the txDelta framework
 
 @note
 
 @todo
 
 @bug 
 
 Copyright notice
 */

#include "utils/channel.h"

/**
 Constructor - empty for the base class
 */
channel::channel( const char* theType ) : object( theType )
{
  channelID = 0;
  managerID = -1;
  channelType = "base";
  rxCharCallback = 0;
  rxMessCallback = 0;
  txDoneCallback = 0;
  readFileDescriptor = 0;
} // channel

/**
 Destructor - empty for the base class
 */
channel::~channel()
{
} // ~channel

/**
 Standard logging call - produces a generic text version of the object.
 Memory allocation / deleting is handled by this object.
 @return pointer to a string describing the state of the object.  The string has an unspecified lifetime and is not multi-thread safe
 */
std::string channel::toString( )
{
  std::ostringstream oss;
  oss << typeid(*this).name() << this;
	return oss.str();
}	// toString

