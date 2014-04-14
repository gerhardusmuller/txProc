/** @class object
 Base Class for most system classes - provides at least for a generic toString function
 
 $Id: object.cpp 1 2009-08-17 12:36:47Z gerhardus $
 Versioning: a.b.c a is a major release, b represents changes or new features, c represents bug fixes. 
 @version 1.0.0		07/06/2006		Gerhardus Muller		Script created
 @version 2.0.0   09/09/2008    Gerhardus Muller    Shared component

 @note

 @todo
 
 @bug

	Copyright Notice
 */

#include "utils/object.h"

/**
 Construction
 */
object::object( const char* objname )
  : log( logger::getInstance( objname ) )
{
}	// Object

/**
 Destruction
 */
object::~object()
{
  delete &log;
}	// ~Object

/**
 Standard logging call - produces a generic text version of the object.
 Memory allocation / deleting is handled by this object.
 @return pointer to a string describing the state of the object.  The string has an unspecified lifetime and is not multi-thread safe
 */
std::string object::toString( )
{
  std::ostringstream oss;
  oss << typeid(*this).name() << ":0x" << this;
	return oss.str();
}	// toString

