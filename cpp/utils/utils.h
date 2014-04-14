/**
 utils - collection of utilities
 
 $Id: utils.h 2629 2012-10-19 16:52:17Z gerhardus $
 Versioning: a.b.c a is a major release, b represents changes or new features, c represents bug fixes. 
 @version 1.0.0		20/04/2010		Gerhardus Muller		Script created

 @note

 @todo
 
 @bug

	Copyright Notice
 */

#if !defined( utils_defined_ )
#define utils_defined_

#include "utils/object.h"
#include <sys/types.h>
#include <unistd.h>

class utils : public object
{
  // Definitions
  public:

    // Methods
  public:
    utils( );
    ~utils( );
    static int dropPriviledgeTemp( uid_t newUid );
    static int dropPriviledgePerm( uid_t newUid );
    static int dropPriviledgePerm( );
    static int restorePriviledge( );
    static bool setFileOwnership( int fd, const char* owner, const char* group );
    static bool setFileOwnership( const char* fname, const char* owner, const char* group );
    static bool lookupUserId( const char* owner, const char* group, int& userId, int& groupId );
    static std::string timeToString( time_t t );
    static std::string printExitStatus( int status );
    static std::string hexEncode( const std::string& strIn );
    static std::string hexDecode( const std::string& strIn );
    static std::string base64Encode( const std::string& strIn );
    static std::string base64Decode( const std::string& strIn );
    static void stripTrailingCRLF( std::string& str, bool bAll=false );

  private:
    static void encodeBlock( const unsigned char* in, unsigned char* out, int len );
    static void decodeblock( unsigned char* in, unsigned char* out );
    static int hexCharToDigit( char x );

      // Properties
  public:

  protected:
    static logger               log;           ///< class scope logger

  private:
};	// class utils

  
#endif // !defined( utils_defined_)

