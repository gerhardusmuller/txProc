/**
 baseEvent - basic event class
 
 $Id: baseEvent.cpp 2946 2013-12-10 10:00:08Z gerhardus $
 Versioning: a.b.c a is a major release, b represents changes or new features, c represents bug fixes. 
 @version 1.0.0		22/09/2009		Gerhardus Muller		Script created
 @version 1.1.0		11/02/2011		Gerhardus Muller		Enhanced error json handling in parse part1/part2
 @version 1.1.0		17/04/2012		Gerhardus Muller		added a workerPid field in part2
 @version 1.2.0		27/02/2013		Gerhardus Muller		support for fragmented serialisation to and from a streaming socket 

 @note

 @todo
 
 @bug

	Copyright Notice
 */

#include <ctype.h>
#include <stdexcept>
#include "application/baseEvent.h"
#include "application/recoveryLog.h"
#include "utils/utils.h"

const char* baseEvent::PROTOCOL_VERSION_NUMBER = "3.0";
const char* baseEvent::FRAME_HEADER = "#frameNewframe#v";
recoveryLog* baseEvent::theRecoveryLog = NULL;
logger baseEvent::staticLogger = logger( "eventS", loggerDefs::MIDLEVEL );

/**
 * constructor
 * **/
baseEvent::baseEvent( )
  : object( "baseEvent" ),
    execParamsConst(execParams)
{
  bPart1Extracted = true;     // we assume this by virtue of construction
  bPart1JsonValid = false;
  bPart2Extracted = false;
  bPart2JsonValid = false;
  bSysParamsExtracted = false;
  bSysParamJsonValid = false;
  bExecParamsExtracted = false;
  bExecParamJsonValid = false;
  eventType = EV_UNKNOWN;
  queueTime = 0;
  readyTime = 0;
  bExpired = false;
  subQueue = 0;
  retries = 0;
  returnFd = "-1";
  expiryTime = 0;
  lifetime = -1;
  workerPid = -1;
} // baseEvent

baseEvent::baseEvent( eEventType type, const char* queue, const char* objName )
  : object( objName ),
    execParamsConst(execParams)
{
  bPart1Extracted = true;     // we assume this by virtue of construction
  bPart1JsonValid = false;
  bPart2Extracted = false;
  bPart2JsonValid = false;
  bSysParamsExtracted = false;
  bSysParamJsonValid = false;
  bExecParamsExtracted = false;
  bExecParamJsonValid = false;
  eventType = type;
  log.setInstanceName( typeToString() );
  if( queue != NULL ) destQueue = queue;
  queueTime = 0;
  readyTime = 0;
  bExpired = false;
  subQueue = 0;
  retries = 0;
  returnFd = "-1";
  expiryTime = 0;
  lifetime = -1;
  workerPid = -1;
} // baseEvent

baseEvent::baseEvent( const char* body, const char* objName )
  : object( objName ),
    execParamsConst(execParams)
{
  eventType = EV_UNKNOWN;
  queueTime = 0;
  readyTime = 0;
  bExpired = false;
  subQueue = 0;
  retries = 0;
  returnFd = "-1";
  expiryTime = 0;
  lifetime = -1;
  workerPid = -1;
  parseBody( body );
  log.setInstanceName( typeToString() );
} // baseEvent

baseEvent::baseEvent( const baseEvent& c )
  : object( c.log.getInstanceName().c_str() ),
    execParamsConst(execParams)
{
}	// baseEvent

/**
 * destructor
 * **/
baseEvent::~baseEvent()
{
} // ~baseEvent

/**
 * serialise to the given stream, write a header line indicating the number of bytes to follow
 * @param fd - socket to use
 * @param bFdType - defaults to FD_SOCKET
 * @return the number of bytes written or -1 for error
 * **/
int baseEvent::serialise( int fd, baseEvent::eFdType fdType )
{
  serialiseToString( );
  
  int ret = 0;
  bool bPipe = false;
  switch( fdType )
  {
    case FD_PIPE:
      bPipe = true;
    case FD_SOCKET:
      {
        ret = unixSocket::writeOnce( fd, strSerialised, bPipe );
        if( ret < 1 )
        {
          if( theRecoveryLog != NULL )
          {
            theRecoveryLog->writeEntry( this, "ser_fail" );
          }
          else
            log.warn( log.LOGALWAYS, "serialise: (theRecoveryLog is NULL) failed for %s", toString().c_str() );
        } // error handling
      } // case FD_SOCKET
      break;
    case FD_FILE:
      ret = write( fd, (const void*)strSerialised.c_str(), strSerialised.length() );
      break;
  } // switch
  return ret;
} // serialise

/**
 * invokes serialise and returns any data not sent
 * @param fd - socket to use
 * @param bFdType - defaults to FD_SOCKET
 * @return -1 for error, 1 for success (entire packet written) or 0 if a part packet is written. In this case strSerialised contains the balance
 * **/
int baseEvent::serialiseNonBlock( int fd, baseEvent::eFdType fdType )
{
  int bytesWritten = serialise( fd, fdType );
  if( bytesWritten == -1 ) return -1;
  if( (unsigned int)bytesWritten >= strSerialised.length() ) return 1;      // we have to test for >= otherwise if there is a mess substr will throw
  strSerialised = strSerialised.substr( bytesWritten );       // dump the part that has been written
  return 0;
} // serialiseNonBlock

/**
 * unserialises from a file descriptor
 * **/
baseEvent* baseEvent::unSerialise( unixSocket *fd )
{
  char header[128];

  int bytesReceived = fd->read( header, FRAME_HEADER_LEN );   // read the frame header, protocol version and payload length including the \n at the end
  if( bytesReceived == -1 ) return NULL;                      // nothing available

  // the current implementation is not built to handle a fragmented header
  // ie don't use over the internet
  header[bytesReceived] = '\0';
  if( bytesReceived != FRAME_HEADER_LEN )
    throw Exception( staticLogger, staticLogger.WARN, "unSerialise: expected the frame header, got %d bytes:'%s'", bytesReceived, header );

  int packetLen = 0;
  int numParsed = 0;
  char headerTemplate[128];
  sprintf( headerTemplate, "%s%s:%cu", FRAME_HEADER,PROTOCOL_VERSION_NUMBER,toascii('%') );
  numParsed = sscanf( header, headerTemplate, &packetLen );
  if( numParsed != 1 )
    throw Exception( staticLogger, staticLogger.WARN, "unSerialise: failed to parse frame header, got %d bytes:'%s'", bytesReceived, header );

  // unserialise if the body can be read
  char* body = new char[packetLen+1];
  baseEvent* pEvent = NULL;
  try
  {
    int bytesReceived = 0;
    if( packetLen > 0 )
      bytesReceived = fd->read( body, packetLen );
    if( bytesReceived != packetLen )
    {
      // will try again when there are more bytes available
      staticLogger.debug( loggerDefs::MIDLEVEL, "unSerialise bytesReceived(%d) != packetLen(%d)", bytesReceived, packetLen );
      if( bytesReceived > 0 ) fd->returnUnusedCharacters( body, bytesReceived ); // this includes any left-over characters from the resync operation
      fd->returnUnusedCharacters( header, FRAME_HEADER_LEN ); // and in front of whatever we put back the header we read
      if(staticLogger.wouldLog(staticLogger.LOGSELDOM)) staticLogger.debug( loggerDefs::MIDLEVEL, "unSerialise failed to read body - read %d of %d - %s", bytesReceived, packetLen, body );
      return NULL;
    } // if

    pEvent = new baseEvent( body );
    delete[] body;
  } // try
  catch( Exception e )
  {
    delete[] body;
    throw;
  } // catch

  if( staticLogger.wouldLog(loggerDefs::LOGSELDOM) ) staticLogger.debug() << "baseEvent::unSerialise: " << pEvent->toString();
  return pEvent;
} // unSerialise

/**
 * un-serialise from file - expects one object per file
 * @param the filename
 * @return the newly constructed object or NULL on failure
 * **/
baseEvent* baseEvent::unSerialiseFromFile( const char* fn )
{
  std::ifstream ifs( fn );
  if( !ifs.good() )
    throw Exception( staticLogger, loggerDefs::ERROR, "unSerialiseFromFile: failed to open file '%s'", fn );

  // we don't read and use the record header with the number of bytes other than for verification
  // assume C++ streams per se a re reliable
  std::string header;
  getline( ifs, header );
  int packetLen = 0;
  char headerTemplate[128];
  sprintf( headerTemplate, "%s%s:%cu", FRAME_HEADER,PROTOCOL_VERSION_NUMBER, toascii('%') );
  int numParsed = sscanf( header.c_str(), headerTemplate, &packetLen );
  if( numParsed != 1 )
    throw Exception( staticLogger, loggerDefs::ERROR, "unSerialiseFromFile: failed to parse header line '%s', file '%s'", header.c_str(), fn );

  // unserialise if the body can be read
  char* body = new char[packetLen+1];
  baseEvent* pEvent = NULL;
  int bytesReceived = 0;
  if( packetLen > 0 )
  {
    ifs.read( body, packetLen );
    bytesReceived = ifs.gcount();
  } // if
  if( bytesReceived != packetLen )
    throw Exception( staticLogger, loggerDefs::INFO, "unSerialiseFromFile: failed to read body - read %d of %d", bytesReceived, packetLen );

  pEvent = new baseEvent( body );
  delete[] body;

  ifs.close( );
  return pEvent;
} // unSerialiseFromFile

/**
 * un-serialise from a string
 * @param the filename
 * @return the newly constructed object or NULL on failure (including it not being a baseEvent serialised packet at all)
 * @exception - only only finding a valid header but not being able to deserialise properly
 * **/
baseEvent* baseEvent::unSerialiseFromString( const std::string& packet )
{
  int packetLen = 0;
  char headerTemplate[128];
  sprintf( headerTemplate, "%s%s:%cu", FRAME_HEADER,PROTOCOL_VERSION_NUMBER, toascii('%') );
  int numParsed = sscanf( packet.c_str(), headerTemplate, &packetLen );
  if( numParsed != 1 ) return NULL;

  baseEvent* pEvent = NULL;
  const char* body = &packet.c_str()[FRAME_HEADER_LEN];
  pEvent = new baseEvent( body );
  return pEvent;
} // unSerialiseFromString

/**
 * parses a frame or message body
 * hardcoded to match serialiseToString - can later on be rewritten to support a 
 * derived event class or with section types other than json
 * @param body
 * **/
void baseEvent::parseBody( const char* body )
{
  eventType = EV_UNKNOWN;
  unsigned int numSections = 0;
  unsigned int part1Size = 0;
  unsigned int part2Size = 0;
  unsigned int sysSize = 0;
  unsigned int execSize = 0;
  int numParams = sscanf( body, "%02u,1,%06u,1,%06u,1,%06u,1,%06u\n",&numSections,&part1Size,&part2Size,&sysSize,&execSize );

  if( numParams != 5 ) throw Exception( log, log.WARN, "parseBody: only parsed %d parameters from frame:'%s'", numParams, body );
  if( numSections != 4 ) throw Exception( log, log.WARN, "parseBody: expected 4 sections found %d", numSections );
  if( part1Size == 0 ) throw Exception( log, log.WARN, "parseBody: part1 cannot be empty" );

  int startOffset = BLOCK_HEADER_LEN;
  jsonPart1.assign( &body[startOffset], part1Size );
  startOffset += part1Size;
  bPart1Extracted = false;
  bPart1JsonValid = true;
  parsePart1();

  if( part2Size > 0 )
  {
    jsonPart2.assign( &body[startOffset], part2Size );
    startOffset += part2Size;
  } // if
  bPart2Extracted = false;
  bPart2JsonValid = true;

  if( sysSize > 0 )
  {
    jsonSysParams.assign( &body[startOffset], sysSize );
    startOffset += sysSize;
  } // if
  bSysParamsExtracted = false;
  bSysParamJsonValid = true;

  if( execSize > 0 )
  {
    jsonExecParams.assign( &body[startOffset], execSize );
    startOffset += execSize;
  } // if
  bExecParamsExtracted = false;
  bExecParamJsonValid = true;

  log.info( log.LOGONOCCASION, "parseBody: sections:%u part1Size:%u part2Size:%u sysSize:%u execSize:%u",numSections,part1Size,part2Size,sysSize,execSize );
} // parseBody

/**
 * serialise to the given file descriptor
 * @param fd 
 * @return the number of bytes written or -1 for error
 * **/
int baseEvent::serialiseToFile( int fd )
{
  std::string buf = serialiseToString();
  int ret = write( fd, (const void*)buf.c_str(), buf.length() );

  return ret;
} // serialiseToFile

/**
 * serialise the object to a string
 * create jsonPart1, jsonPart2, jsonSysParams, jsonExecParams
 * assume the jsonxxx string to contain the correct json if it was never extracted
 * **/
std::string& baseEvent::serialiseToString( )
{
  // create part1
  if( !bPart1JsonValid )
    serialisePart1();

  // create part2
  if( !bPart2JsonValid )
    serialisePart2();

  // create jsonSysParams
  if( !bSysParamJsonValid )
    serialiseSysParam();

  // create jsonExecParams
  if( !bExecParamJsonValid )
    serialiseExecParam();

  // construct the payload - 4 payloads all of type 1 which is a json payload
  // this can later be rewritten to make it possible for a derived class to add extra blocks
  char payloadHeader[128];
  int payloadLen = BLOCK_HEADER_LEN+jsonPart1.size()+jsonPart2.size()+jsonSysParams.size()+jsonExecParams.size();
  if( ((unsigned int)payloadLen>MAX_HEADER_BLOCK_LEN)||(jsonPart1.size()>MAX_HEADER_BLOCK_LEN)||(jsonPart2.size()>MAX_HEADER_BLOCK_LEN)||(jsonSysParams.size()>MAX_HEADER_BLOCK_LEN)||(jsonExecParams.size()>MAX_HEADER_BLOCK_LEN))
    throw Exception( log, log.WARN, "serialiseToString:MAX_HEADER_BLOCK_LEN exceeded: payloadLen:%u, jsonPart1:%u, jsonPart2:%u, jsonSysParams:%u, jsonExecParams:%u",payloadLen,(unsigned int)jsonPart1.size(),(unsigned int)jsonPart2.size(),(unsigned int)jsonSysParams.size(),(unsigned int)jsonExecParams.size() );
  sprintf( payloadHeader, "%s%s:%06u\n%02u,1,%06u,1,%06u,1,%06u,1,%06u\n",FRAME_HEADER,PROTOCOL_VERSION_NUMBER,payloadLen,4,(unsigned int)jsonPart1.size(),(unsigned int)jsonPart2.size(),(unsigned int)jsonSysParams.size(),(unsigned int)jsonExecParams.size() );
  strSerialised = payloadHeader;
  strSerialised.append(jsonPart1).append(jsonPart2).append(jsonSysParams).append(jsonExecParams);

  if( log.wouldLog(log.LOGSELDOM) ) log.debug() << "baseEvent::serialiseToString: " << strSerialised;
  return strSerialised;
} // serialiseToString

/**
 * **/
void baseEvent::serialisePart1( )
{
  Json::Value part1;
  part1["eventType"] = (int)eventType;
  if( !reference.empty() ) part1["reference"] = reference;
  if( !returnFd.empty() && (returnFd.compare("-1")!=0) ) part1["returnFd"] = returnFd;
  log.debug( log.LOGSELDOM, "serialisePart1: rFd:'%s'", returnFd.c_str() );
  if( !destQueue.empty() ) part1["destQueue"] = destQueue;
  Json::FastWriter writer;
  jsonPart1 = writer.write( part1 );
  bPart1JsonValid = true;
} // serialisePart1

/**
 * **/
void baseEvent::parsePart1( )
{
  if( bPart1Extracted ) return;
  //destQueue = "default";

  if( !jsonPart1.empty() )
  {
    Json::Value root;
    Json::Reader reader;
    bool parsingSuccessful = reader.parse( jsonPart1, root );
    if ( !parsingSuccessful ) throw Exception( log, log.WARN, "parsePart1: failed to parse:'%s'", jsonPart1.c_str() );

    try
    {
      if( root.isMember("eventType") ) eventType = (eEventType)root.get("eventType", (int)EV_UNKNOWN ).asInt();
      if( root.isMember("reference") ) reference = root.get( "reference", Json::Value() ).asString();
      if( root.isMember("returnFd") ) returnFd = root.get( "returnFd", "-1" ).asString();
      if( root.isMember("destQueue") ) destQueue = root.get( "destQueue", "default" ).asString();
    } // try
    catch( std::runtime_error e )
    { // json-cpp throws runtime_error
      throw Exception( log, log.WARN, "parsePart1: json-cpp exception:%s part1:'%s'", e.what(), jsonPart1.c_str() );
    } // catch
  } // if

  bPart1Extracted = true;
} // parsePart1

/**
 * **/
void baseEvent::serialisePart2( )
{
  Json::Value part2;
  if( !trace.empty() ) part2["trace"] = trace;
  if( !traceTimestamp.empty() ) part2["traceTimestamp"] = traceTimestamp;
  if( expiryTime != 0 ) part2["expiryTime"] = expiryTime;
  if( lifetime != -1 ) part2["lifetime"] = lifetime;
  if( retries != 0 ) part2["retries"] = retries;
  if( workerPid != -1 ) part2["wpid"] = workerPid;
  if( !part2.empty() )
  {
    Json::FastWriter writer;
    jsonPart2 = writer.write( part2 );
  } // else
  else
    jsonPart2.empty();
  bPart2JsonValid = true;
} // serialisePart2

/**
 * **/
void baseEvent::parsePart2( )
{
  if( bPart2Extracted ) return;

  if( !jsonPart2.empty() )
  {
    Json::Value root;
    Json::Reader reader;
    bool parsingSuccessful = reader.parse( jsonPart2, root );
    if ( !parsingSuccessful ) throw Exception( log, log.WARN, "parsePart2: failed to parse:'%s'", jsonPart2.c_str() );

    try
    {
      if( root.isMember("trace") ) trace = root.get( "trace", Json::Value() ).asString();
      if( root.isMember("traceTimestamp") ) traceTimestamp = root.get( "traceTimestamp", Json::Value() ).asString();
      if( root.isMember("expiryTime") ) expiryTime = root.get("expiryTime", 0 ).asUInt();
      if( root.isMember("lifetime") ) lifetime = root.get("lifetime", -1 ).asInt();
      if( root.isMember("retries") ) retries = root.get("retries", 0 ).asInt();
      if( root.isMember("wpid") ) workerPid = root.get("wpid", 0 ).asInt();
    } // try
    catch( std::runtime_error e )
    { // json-cpp throws runtime_error
      throw Exception( log, log.WARN, "parsePart2: json-cpp exception:'%s' part2:'%s'", e.what(), jsonPart2.c_str() );
    } // catch
  } // if

  bPart2Extracted = true;
} // parsePart2

/**
 * **/
void baseEvent::serialiseSysParam( )
{
  Json::FastWriter writer;
  if( !sysParams.empty() )
    jsonSysParams = writer.write( sysParams );
  else
    jsonSysParams = "";
  bSysParamJsonValid = true;
} // serialiseSysParam

/**
 * **/
void baseEvent::parseSysParams( )
{
  if( bSysParamsExtracted ) return;
  if( !jsonSysParams.empty() )
  {
    Json::Reader reader;
    bool parsingSuccessful = reader.parse( jsonSysParams, sysParams );
    if ( !parsingSuccessful ) throw Exception( log, log.WARN, "parseSysParams: failed to parse:'%s'", jsonSysParams.c_str() );
  } // if
  bSysParamsExtracted = true;
} // parseSysParams

/**
 * **/
void baseEvent::serialiseExecParam( )
{
  Json::FastWriter writer;
  if( !execParams.empty() )
    jsonExecParams = writer.write( execParams );
  else
    jsonExecParams = "";
  bExecParamJsonValid = true;
} // serialiseExecParam

/**
 * **/
void baseEvent::parseExecParams( )
{
  if( bExecParamsExtracted ) return;
  if( !jsonExecParams.empty() )
  {
    Json::Reader reader;
    bool parsingSuccessful = reader.parse( jsonExecParams, execParams );
    if ( !parsingSuccessful ) throw Exception( log, log.WARN, "parseExecParams: failed to parse:'%s'", jsonExecParams.c_str() );
  } // if
  bExecParamsExtracted = true;
} // parseExecParams

/**
 * returns the current fd reference - tacked onto the fd with a ';'
 * has to occur before the record separator being a ':'
 * @return the reference or and empty string
 * **/
void baseEvent::getReturnFdRef( std::string& ref )
{
  std::string::size_type t;
  if( (t=returnFd.find(':'))!=std::string::npos )   // record separator
  {
    std::string::size_type n = returnFd.find(';');  // ref separator
    if( (n!=std::string::npos) && (n<t) )
      ref = std::string( returnFd, n+1, t-n-1 );
  }
} // getReturnFdRef
void* baseEvent::getReturnFdRefAsVoidP( )
{
  void* p = NULL;
  std::string::size_type t;
  if( (t=returnFd.find(':'))!=std::string::npos )   // record separator
  {
    std::string::size_type n = returnFd.find(';');  // ref separator
    if( (n!=std::string::npos) && (n<t) )
    {
      const char* refP = returnFd.c_str();
      p = (void*)strtoul( refP+n+1, NULL, 16 );
    } // if
  } // if
  return p;
} // getReturnFdRefAsVoidP

/**
 * parses destQueue into mainQueue and an optional subQueue separated from
 * the main queue name by a ';'
 * **/
void baseEvent::parseMainDestQueue( )
{
  std::string::size_type t;
  if( (t=destQueue.find(';'))!=std::string::npos )   // separator
  {
    mainQueue.assign( destQueue, 0, t );
    std::string tmp;
    tmp.assign( destQueue, t, destQueue.length()-t-1 );
    subQueue = strtoul( tmp.c_str(), NULL, 10 );
    log.debug( log.LOGONOCCASION, "parseMainDestQueue: destQueue:'%s' mainQueue:'%s' subQueue:%lu", destQueue.c_str(), mainQueue.c_str(), subQueue );
  } // if
  else
  {
    mainQueue = destQueue;
    subQueue = 0;
  } // else
} // parseMainDestQueue

/**
 * @return true if event is expired
 * @param now - the current time
 * **/
bool baseEvent::isExpired( unsigned int now )
{
  if( !bPart2Extracted ) parsePart2();
  if( (expiryTime != 0) && (expiryTime < now) )
    return true;
  else
    return false;
} // isExpired

/**
 * prepends a return routing parameter with or without a reference
 * the reference is not allowed to contain either a ':' or a ';'
 * @param returnFd
 * @param ref
 * **/
void baseEvent::addReturnRouting( int theReturnFd, void* ref )
{
  char refStr[64];
  refStr[0] = '\0';
  if( ref != NULL ) sprintf( refStr, "%p", ref );
  addReturnRouting( theReturnFd, refStr );
} // addReturnRouting
void baseEvent::addReturnRouting( int theReturnFd, const char* ref )
{
  std::string newFdRefString;
  char refStr[64];
  sprintf( refStr, "%d", theReturnFd );
  newFdRefString.append( refStr );
  if( (ref!=NULL) && (ref[0]!='\0') )
  {
    newFdRefString.append( ";" );
    newFdRefString.append( ref );
  } // if
  if( !returnFd.empty() )
  {
    newFdRefString.append( ":" );
    returnFd = newFdRefString.append( returnFd );
  } // if
  else
    returnFd = newFdRefString;

  bPart1JsonValid = false;
} // addReturnRouting

/**
 * standard logging call - produces a generic text version of the object
 * **/
std::string baseEvent::toString( )
{
  std::ostringstream oss;
  oss << typeToString();
  if( !destQueue.empty() ) oss << " q:'" << destQueue << "' ";
  if( !reference.empty() ) oss << " ref:'" << reference << "' ";
  if( !returnFd.empty() && (returnFd.compare("-1")!=0) ) oss << " rFd:" << returnFd;
  
  if( bSysParamsExtracted )
  {
    if( sysParams.isMember("bStandardResponse") ) {if(getStandardResponse()) oss << " stdRes";}
    if( sysParams.isMember("command") ) oss << " cmd:" << commandToString();
    if( sysParams.isMember("url") ) oss << " url:" << getUrl();
    if( sysParams.isMember("scriptName") ) oss << " scriptName:" << getScriptName();
    if( sysParams.isMember("result") ) oss << " result:" << getResult();
    if( sysParams.isMember("bSuccess") ) oss << " bSuccess:" << isSuccess();
    if( sysParams.isMember("errorString") ) oss << " errorString:" << getErrorString();
    if( sysParams.isMember("failureCause") ) oss << " failureCause:" << getFailureCause();
    if( sysParams.isMember("systemParam") ) oss << " systemParam:" << getSystemParam();
  } // if bSysParamsExtracted
  else if( bSysParamJsonValid )
  {
    std::string str = jsonSysParams;
    utils::stripTrailingCRLF( str, false );
    oss << " sysParams:" << str;
  } // else

  if( bExecParamsExtracted && !bExecParamJsonValid )  serialiseExecParam();
  if( !jsonExecParams.empty() )
  {
    std::string str = jsonExecParams;
    utils::stripTrailingCRLF( str, false );
    oss << " execParams:" << str;
  } // if

  if( bPart2Extracted )
  {
    if( expiryTime != 0 ) oss << " expires:" << expiryTime;
    if( bExpired ) oss << " expired";
    if( lifetime != -1 ) oss << " lifetime:" << lifetime;
    if( retries > 0 ) oss << " retries:" << retries;
  } // if part2
  else if( !jsonPart2.empty() )
  {
    std::string str = jsonPart2;
    utils::stripTrailingCRLF( str, false );
    if( !str.empty() ) oss << " part2:" << str;
  } // else

  if( readyTime != 0 ) oss << " readyTime:" << readyTime;
  if( !trace.empty() ) oss << " traceB||" << trace << "||traceE ";
  if( !traceTimestamp.empty() ) oss << " traceTS:" << traceTimestamp;
  oss << " p:" << this;

	return oss.str();
}	// toString
std::string baseEvent::toStringBrief( )
{
  std::ostringstream oss;
  oss << typeToString();
  if( !destQueue.empty() ) oss << " q:'" << destQueue << "' ";
  if( !reference.empty() ) oss << " ref:'" << reference << "' ";
  if( !returnFd.empty() && (returnFd.compare("-1")!=0) ) oss << " rFd:" << returnFd;
  
  if( bSysParamsExtracted )
  {
    if( sysParams.isMember("command") ) oss << " cmd:" << commandToString();
    if( sysParams.isMember("url") ) oss << " url:" << getUrl();
    if( sysParams.isMember("scriptName") ) oss << " scriptName:" << getScriptName();
    if( sysParams.isMember("result") ) oss << " result:" << getResult();
    if( sysParams.isMember("bSuccess") ) oss << " bSuccess:" << isSuccess();
    if( sysParams.isMember("errorString") ) oss << " errorString:" << getErrorString();
    if( sysParams.isMember("failureCause") ) oss << " failureCause:" << getFailureCause();
    if( sysParams.isMember("systemParam") ) oss << " systemParam:" << getSystemParam();
  } // if bSysParamsExtracted
  else if( bSysParamJsonValid )
  {
    std::string str = jsonSysParams;
    utils::stripTrailingCRLF( str, false );
    oss << " sysParams:" << str;
  } // else

  if( !trace.empty() ) oss << " traceB||" << trace << "||traceE ";
  if( !traceTimestamp.empty() ) oss << " traceTS:" << traceTimestamp;

	return oss.str();
}	// toStringBrief

/**
 * **/
const char* baseEvent::typeToString( )
{
  switch( eventType )
  {
    case EV_UNKNOWN:
      return "EV_UNKNOWN";
      break;
    case EV_BASE:
      return "EV_BASE";
      break;
    case EV_SCRIPT:
      return "EV_SCRIPT";
      break;
    case EV_PERL:
      return "EV_PERL";
      break;
    case EV_BIN:
      return "EV_BIN";
      break;
    case EV_URL:
      return "EV_URL";
      break;
    case EV_RESULT:
      return "EV_RESULT";
      break;
    case EV_WORKER_DONE:
      return "EV_WORKER_DONE";
      break;
    case EV_COMMAND:
      return "EV_COMMAND";
      break;
    case EV_REPLY:
      return "EV_REPLY";
      break;
    case EV_ERROR:
      return "EV_ERROR";
      break;
    default:
      return "unknown eventType";
  } // switch
} // typeToString

/**
 * **/
const char* baseEvent::commandToString( )
{
  eCommandType command = getCommand();
  switch( command )
  {
    case CMD_NONE:
      return "CMD_NONE";
      break;
    case CMD_STATS:
      return "CMD_STATS";
      break;
    case CMD_RESET_STATS:
      return "CMD_RESET_STATS";
      break;
    case CMD_REOPEN_LOG:
      return "CMD_REOPEN_LOG";
      break;
    case CMD_REREAD_CONF:
      return "CMD_REREAD_CONF";
      break;
    case CMD_EXIT_WHEN_DONE:
      return "CMD_EXIT_WHEN_DONE";
      break;
    case CMD_SEND_UDP_PACKET:
      return "CMD_SEND_UDP_PACKET";
      break;
    case CMD_TIMER_SIGNAL:
      return "CMD_TIMER_SIGNAL";
      break;
    case CMD_CHILD_SIGNAL:
      return "CMD_CHILD_SIGNAL";
      break;
    case CMD_APP:
      return "CMD_APP";
      break;
    case CMD_SHUTDOWN:
      return "CMD_SHUTDOWN";
      break;
    case CMD_NUCLEUS_CONF:
      return "CMD_NUCLEUS_CONF";
      break;
    case CMD_DUMP_STATE:
      return "CMD_DUMP_STATE";
      break;
    case CMD_NETWORKIF_CONF:
      return "CMD_NETWORKIF_CONF";
      break;
    case CMD_END_OF_QUEUE:
      return "CMD_END_OF_QUEUE";
      break;
    case CMD_MAIN_CONF:
      return "CMD_MAIN_CONF";
      break;
    case CMD_PERSISTENT_APP:
      return "CMD_PERSISTENT_APP";
      break;
    case CMD_EVENT:
      return "CMD_EVENT";
      break;
    case CMD_WORKER_CONF:
      return "CMD_WORKER_CONF";
      break;
    default:
      return "unknown";
  } // switch
  return "unknown";
} // commandToString
