/** @class utils
 utils - collection of utilities
 
 $Id: utils.cpp 2892 2013-06-26 15:55:45Z gerhardus $
 Versioning: a.b.c a is a major release, b represents changes or new features, c represents bug fixes. 
 @version 1.0.0		02/08/2006		Gerhardus Muller		Script created
 @version 1.1.0		24/05/2011		Gerhardus Muller		ported to Mac
 @version 1.2.0		08/02/2012		Gerhardus Muller		added base64 decode
 @version 1.3.0		20/02/2012		Gerhardus Muller		changed the base64 encode/decode to use the filesystem safe alphabet
 @version 1.3.1		10/04/2012		Gerhardus Muller		added support for non padded base64 strings on decode
 @version 1.4.0		26/06/2013		Gerhardus Muller		changed error handling on setFileOwnership and added default group for user in lookupUserId

 @note

 @todo
 
 @bug

	Copyright Notice
 */

#include "utils/utils.h"
#include <errno.h>
#include <string.h>
#include <grp.h>
#include <pwd.h>
#include <math.h>
#include <unistd.h>
#include <sys/wait.h>
#define ERROR_SYSCALL -1

logger utils::log = logger( "utils", loggerDefs::MIDLEVEL );

/**
 Construction
 */
utils::utils( )
  : object( "utils" )
{
}	// utils

/**
 Destruction
 */
utils::~utils()
{
}	// ~utils

/**
 * convert time into a string
 * @param time value to convert
 * @return string version
 * **/
std::string utils::timeToString( time_t t )
{
  if( t == 0 ) return std::string( );
  char sTime[128];
  struct tm *tmp;
  tmp = localtime( &t );
  strftime( sTime, sizeof(sTime), "%F %T", tmp );
  return std::string( sTime );
} // timeToString

/**
 * priviledge management routines as taken from:
 * Setuid Demystiﬁed, Hao Chen, David Wagner, Drew Dean
 * **/
int utils::dropPriviledgeTemp( uid_t newUid )
{
#ifdef PLATFORM_MAC
  if( seteuid(newUid) < 0 )
    return ERROR_SYSCALL;
  if( geteuid() != newUid )
    return ERROR_SYSCALL;
#else
  if( setresuid( -1, newUid, geteuid() ) < 0 )
    return ERROR_SYSCALL;
  if( geteuid() != newUid )
    return ERROR_SYSCALL;
#endif
  return 0;
} // dropPriviledgeTemp
int utils::dropPriviledgePerm( uid_t newUid )
{
#ifdef PLATFORM_MAC
  if( setuid(newUid) < 0 )
    return ERROR_SYSCALL;
  if( getuid() != newUid )
    return ERROR_SYSCALL;
#else
  uid_t ruid, euid, suid;
  if( setresuid( newUid, newUid, newUid ) < 0 )
    return ERROR_SYSCALL;
  if( getresuid( &ruid, &euid, &suid ) < 0 )
    return ERROR_SYSCALL;
  if( ruid!=newUid || euid!=newUid || suid!=newUid )
    return ERROR_SYSCALL;
#endif
  return 0;
} // dropPriviledgePerm
int utils::dropPriviledgePerm( )
{
#ifdef PLATFORM_MAC
  uid_t newUid = geteuid(); // retrieves the effective user id of the program
  if( setuid(newUid) < 0 )
    return ERROR_SYSCALL;
  if( getuid() != newUid )
    return ERROR_SYSCALL;
#else
  uid_t ruid, euid, suid, newUid;
  if( getresuid( &ruid, &newUid, &suid ) < 0 )
    return ERROR_SYSCALL;
  if( setresuid( newUid, newUid, newUid ) < 0 )
    return ERROR_SYSCALL;
  if( getresuid( &ruid, &euid, &suid ) < 0 )
    return ERROR_SYSCALL;
  if( ruid!=newUid || euid!=newUid || suid!=newUid )
    return ERROR_SYSCALL;
#endif
  return 0;
} // dropPriviledgePerm
// restores priviledges to the real user id
// the original script used the suid rather than the ruid ..
// not what we wanted
int utils::restorePriviledge( )
{
#ifdef PLATFORM_MAC
  uid_t newUid = getuid();  // retrieves the real user id - specified at login time
  if( setuid(newUid) < 0 )
    return ERROR_SYSCALL;
  if( getuid() != newUid )
    return ERROR_SYSCALL;
#else
  uid_t ruid, euid, suid;
  if( getresuid( &ruid, &euid, &suid ) < 0 )
    return ERROR_SYSCALL;
  if( setresuid( -1, ruid, -1 ) < 0 )
    return ERROR_SYSCALL;
  if( geteuid() != ruid )
    return ERROR_SYSCALL;
#endif
  return 0;
} // restorePriviledge

/**
 * set file ownership
 * specify either fd or fname and at least one of owner and group
 * **/
bool utils::setFileOwnership( int fd, const char* owner, const char* group )
{
  if( (owner==NULL) && (group==NULL) ) return true;

  int userId = -1;
  int groupId = -1;
  if( !lookupUserId( owner, group, userId, groupId ) )
    return false;

  if( (userId!=-1) || (groupId!=-1) )
  {
    int retVal = -1;
    retVal = fchown( fd, userId, groupId );
    if( retVal == -1 )
    {
      if( geteuid() == 0 )
        log.warn( log.LOGALWAYS, "setFileOwnership: fchown fd:%d to user:'%s'(%d) group:'%s'(%d) failed - %s", fd, owner, userId, group, groupId, strerror(errno) );
      else
        log.info( log.LOGNORMAL, "setFileOwnership: fchown fd:%d to user:'%s'(%d) group:'%s'(%d) euid is not root", fd, owner, userId, group, groupId );
      return false;
    }
    else
      log.info( log.LOGALWAYS, "setFileOwnership: fchown fd:%d to user:'%s'(%d) group:'%s'(%d) ", fd, owner, userId, group, groupId );
  } // if

  return true;
} // setFileOwnership
bool utils::setFileOwnership( const char* fname, const char* owner, const char* group )
{
  if( (owner==NULL) && (group==NULL) ) return true;

  int userId = -1;
  int groupId = -1;
  if( !lookupUserId( owner, group, userId, groupId ) )
    return false;

  if( (userId!=-1) || (groupId!=-1) )
  {
    int retVal = -1;
    retVal = chown( fname, userId, groupId );
    if( retVal == -1 )
    {
      if( geteuid() == 0 )
        log.warn( log.LOGALWAYS, "setFileOwnership: chown fn:'%s' to user:'%s'(%d) group:'%s'(%d) failed - %s", fname, owner, userId, group, groupId, strerror(errno) );
      else
        log.info( log.LOGNORMAL, "setFileOwnership: chown fn:'%s' to user:'%s'(%d) group:'%s'(%d) euid is not root", fname, owner, userId, group, groupId );
      return false;
    } // if
    else
      log.info( log.LOGALWAYS, "setFileOwnership: chown fn:'%s' to user:'%s'(%d) group:'%s'(%d)", fname, owner, userId, group, groupId );
  } // if

  return true;
} // setFileOwnership
bool utils::lookupUserId( const char* owner, const char* group, int& userId, int& groupId )
{
  if( (owner==NULL) && (group==NULL) ) return true;

  if( (owner!=NULL) && (strlen(owner)>0) )
  {
    struct passwd* pw = getpwnam( owner );
    if( pw == NULL )
    {
      log.warn( log.LOGALWAYS, "lookupUserId: could not find user:'%s'", owner );
      return false;
    }
    else
    {
      userId = pw->pw_uid;
      groupId = pw->pw_gid;
    } // else
  } // if
  if( (group!=NULL) && (strlen(group)>0) )
  {
    struct group* gr = getgrnam( group );
    if( gr == NULL )
    {
      log.warn( log.LOGALWAYS, "lookupUserId: could not find group:'%s'", group );
      return false;
    }
    else
    {
      groupId = gr->gr_gid;
    } // else
  } // if

  return true;
} // setFileOwnership

/**
 * print the exit status of a child
 * @param status
 * **/
std::string utils::printExitStatus( int status )
{
  std::ostringstream oss;
  if( WIFEXITED( status ) )
    oss << "normal exit code " << WEXITSTATUS( status );
  else if( WIFSIGNALED( status ) )
    oss << "child killed by signal " << strsignal( WTERMSIG( status ) );
  else if( WIFSTOPPED( status ) )
    oss << "child stopped by signal " << strsignal( WSTOPSIG( status ) );
  else if( WIFCONTINUED( status ) )   // only available in kernel 2.6.10 or later
    oss << "child continued";
  return oss.str();
} // printExitStatus

/**
 * hex encodes a string
 * **/
std::string utils::hexEncode( const std::string& strIn )
{
  std::ostringstream oss;
  char byteInHex[4];
  for( unsigned i=0; i < strIn.length(); i++ )
  {
    sprintf( byteInHex, "%02x", strIn[i]&0xff );
    oss << byteInHex;
  } // for
  return oss.str();
} // hexEncode

/**
 * decodes a hex string
 * **/
std::string utils::hexDecode( const std::string& strIn )
{
  std::ostringstream oss;
  const char* hexStr = strIn.c_str();
  for( unsigned i=0; i < strIn.length(); i+=2 )
  {
    int c = hexCharToDigit(hexStr[i+1]) | hexCharToDigit(hexStr[i])<<4;
    oss << (char)(c & 0xff);
  } // for 
  return oss.str();
} // hexDecode

/**
 * **/
int utils::hexCharToDigit( char x )
{
  if( (x>='0') && (x<='9') ) return x-'0';
  if( (x>='A') && (x<='F') ) return x-'A'+10;
  if( (x>='a') && (x<='f') ) return x-'a'+10;
  return 0; // actually an error condition
} // hexCharToDigit

/**
 * Translation Table as described in RFC1113
 * **/
//static const unsigned char cb64[]="ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
//static const unsigned char cd64[]="|$$$}rstuvwxyz{$$$$$$$>?@ABCDEFGHIJKLMNOPQRSTUVW$$$$$$XYZ[\\]^_`abcdefghijklmnopq";
////                                 +'-./0123456789:;<=>?@ABCDEFGHIJKLMNOPQRSTUVWXYZ[\]^_`abcdefghijklmnopqrstuvwxyz{
// adapted for url safe ie '+'=>'-' and '/'=>'_'
// max limits on reverse lookup should now be 124 not 122
// the decoding table should support both encoding ways
static const unsigned char cb64[]="ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-_";
static const unsigned char cd64[]="|$|$}rstuvwxyz{$$$$$$$>?@ABCDEFGHIJKLMNOPQRSTUVW$$$$}$XYZ[\\]^_`abcdefghijklmnopq";
//                                 +'-./0123456789:;<=>?@ABCDEFGHIJKLMNOPQRSTUVWXYZ[\]^_`abcdefghijklmnopqrstuvwxyz{


/**
 * base64 encodes a string
 * encoding / decoding from 
 * http://base64.sourceforge.net/b64.c
LICENCE:        Copyright (c) 2001 Bob Trower, Trantor Standard Systems Inc.

                Permission is hereby granted, free of charge, to any person
                obtaining a copy of this software and associated
                documentation files (the "Software"), to deal in the
                Software without restriction, including without limitation
                the rights to use, copy, modify, merge, publish, distribute,
                sublicense, and/or sell copies of the Software, and to
                permit persons to whom the Software is furnished to do so,
                subject to the following conditions:

                The above copyright notice and this permission notice shall
                be included in all copies or substantial portions of the
                Software. 
 * @param strIn - the string to be encoded
 * @return the encoded string
 * **/
std::string utils::base64Encode( const std::string& strIn )
{
  int lengthIn = strIn.size();
  int count = 0;
  int outLength = 0;
  int len = 0;

  const unsigned char* inPointer = (unsigned char*)strIn.c_str();
  unsigned char* outBuffer = new unsigned char[2*lengthIn];
  unsigned char* outPointer = outBuffer;

  while( count < lengthIn )
  {
    if( count+3 <= lengthIn )
      len = 3;
    else
      len = lengthIn - count;
    
    encodeBlock( inPointer, outPointer, len );
    inPointer += 3;
    count += 3;
    outPointer += 4;
    outLength += 4;
  } // while
  
  *outPointer = '\0';

  std::string strOut( (const char*)outBuffer, outLength );
  delete outBuffer;
  return strOut;
} // base64Encode

/**
 * base64 decodes a string
 * modified to handle unpadded sequences
 * @param strIn - the string to be decoded
 * @return the decoded string
 * **/
std::string utils::base64Decode( const std::string& strIn )
{
  unsigned char in[4], v;
  int i, len;
  int count = 0;
  int outLength = 0;

  int lengthIn = strIn.size();
  const char* inPointer = strIn.c_str();
  char* outBuffer = new char[lengthIn];
  unsigned char* outPointer = (unsigned char*)outBuffer;

  // pad the base64 string with '=' as required
  int numPadChars = ceil(lengthIn/4.0)*4-lengthIn;
  int paddedLengthIn = lengthIn+numPadChars;
//fprintf(stderr,"lengthIn:%d numPadChars:%d paddedLengthIn:%d\n",lengthIn,numPadChars,paddedLengthIn);
  while( count < paddedLengthIn ) 
  {
    for( len=0, i=0; i<4 && (count<paddedLengthIn); i++ ) 
    {
      v = 0;
      if( count < lengthIn )
        v = (unsigned char) (*inPointer++);
      else
        v = '=';                                                    // pad unpadded sequences
      count++;
//fprintf(stderr,"cnt:%d '%c'\n",count,v);
      v = (unsigned char) ((v < 43 || v > 124) ? 0 : cd64[v-43]);   // 43=>'+' - 122=>'z'; upper limit is now 124=>'|'; 18=>'='
      if(v) v = (unsigned char) ((v == '$') ? 0 : v - 61);          // $ in the reverse lookup table is a placeholder; 61=>'='
      if( v )
      {
        in[i] = (unsigned char) (v - 1);                            // dus actually 'n v-62 - 62=>'>'
        len++;
      } // if
      else
        in[i] = 0;
//fprintf(stderr,"len:%d '%0x'\n",len,v);
    } // for
//fprintf(stderr,"len:%d\n",len);
    if( len ) 
    {
      decodeblock( in, outPointer );
      outPointer += len-1;
      outLength += len-1;
    } // if len
  } // while

  *outPointer = '\0';
  std::string strOut( outBuffer, outLength );
  delete outBuffer;
  return strOut;
} // base64Decode

/**
 * encodeblock
 *
 * encode 3 8-bit binary bytes as 4 '6-bit' characters
 * **/
void utils::encodeBlock( const unsigned char* in, unsigned char* out, int len )
{
  out[0] = cb64[ in[0] >> 2 ];
  out[1] = cb64[ ((in[0] & 0x03) << 4) | ((in[1] & 0xf0) >> 4) ];
  out[2] = (unsigned char) (len > 1 ? cb64[ ((in[1] & 0x0f) << 2) | ((in[2] & 0xc0) >> 6) ] : '=');
  out[3] = (unsigned char) (len > 2 ? cb64[ in[2] & 0x3f ] : '=');
} // encodeBlock

/**
 * decodeblock
 *
 * decode 4 '6-bit' characters into 3 8-bit binary bytes
 * **/
void utils::decodeblock( unsigned char* in, unsigned char* out )
{
  out[0] = (unsigned char) (in[0] << 2 | in[1] >> 4);
  out[1] = (unsigned char) (in[1] << 4 | in[2] >> 2);
  out[2] = (unsigned char) (((in[2] << 6) & 0xc0) | in[3]);
} // decodeblock

/**
 * strips a trailing cr/lf
 * @param str
 * @param bAll - if true then search the entire message for CRLFs
 * **/
void utils::stripTrailingCRLF( std::string& str, bool bAll )
{
  if( str.empty() ) return;
  if( bAll )
  {
    std::string::size_type pos = 0;
    std::string tokens( "\r\n" );
    while( (pos=str.find_first_of(tokens,pos)) != std::string::npos )
    {
      if( str[pos] == '\r' )
        str.replace( pos, 1, "\\r" );
      if( str[pos] == '\n' )
        str.replace( pos, 1, "\\n" );
      pos++;
    } // while
  } // if bAll
  else
  {
    std::string replacementChars;
    for( int len=str.size()-1; len > 0; len-- )
    {
      if( (str[len]=='\r') )
      {
        str.resize( len );
        replacementChars.insert( 0, "\\r" );
      } // if
      else if( (str[len]=='\n') )
      {
        str.resize( len );
        replacementChars.insert( 0, "\\n" );
      } // if
      else
        break;
    } // for

    if( !replacementChars.empty() )
      str.append( replacementChars );
  } // if bAll
} // stripTrailingCRLF

