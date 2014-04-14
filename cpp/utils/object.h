/**
 Base class for system objects
 
 $Id: object.h 2629 2012-10-19 16:52:17Z gerhardus $
 Versioning: a.b.c a is a major release, b represents changes or new features, c represents bug fixes. 
 @version 1.0.0		07/06/2006		Gerhardus Muller		Script created
 @version 2.0.0   09/09/2008    Gerhardus Muller    Shared component
 @version 2.0.1   19/12/2012    Gerhardus Muller    cannot include txProc options

 @note

 @todo
 
 @bug

	Copyright Notice
 */

#if !defined( object_defined_ )
#define object_defined_

#include "logging/logger.h"
#include <typeinfo>
#include <string>
#include <sstream>
#include "exception/Exception.h"
//#include "../src/options.h"

class object
{
  // Definitions
  public:

    // Methods
  public:
    object( const char* objname );
    virtual ~object();
    virtual std::string toString ();
    virtual void dump( const char* pref, loggerDefs::eLogLevel theLevel=loggerDefs::LOGNORMAL ) { if(!log.wouldLog(theLevel))return;log.info(theLevel) << pref+toString( ); };
    virtual void setLoggingId( const char* newName )        {log.setInstanceName(newName);}
    virtual void setLoggingId( std::string& newName )       {log.setInstanceName(newName);}

  private:

    // Properties
  public:
    logger&                     log;                  ///< class scope logger

  protected:

  private:
};	// class object

#endif // !defined( object_defined_)

