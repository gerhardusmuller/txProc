/**
 baseEvent - basic event class
 
 $Id: baseEvent.h 3065 2014-04-07 18:27:52Z gerhardus $
 Versioning: a.b.c a is a major release, b represents changes or new features, c represents bug fixes. 
 @version 1.0.0		22/09/2009		Gerhardus Muller		Script created
 @version 1.0.1		22/02/2011		Gerhardus Muller		getParam was missing a return true
 @version 1.0.2		14/04/2011		Gerhardus Muller		getParamAsUint was returning an int
 @version 1.1.0		17/04/2012		Gerhardus Muller		added a workerPid field in part2
 @version 1.1.0		14/04/2012		Gerhardus Muller		added setResult( const char* str ) variant
 @version 1.2.0		25/09/2012		Gerhardus Muller		moved the location of baseEvent.*
 @version 1.2.1		19/10/2012		Gerhardus Muller		required stdlib given that txproc options.h is no longer included
 @version 1.3.0		27/02/2013		Gerhardus Muller		support for fragmented serialisation to a streaming socket 
 @version 1.3.1		07/04/2014		Gerhardus Muller		setRecoveryEvent used incorrect key

 @note

 @todo
 
 @bug

	Copyright Notice
 */

#if !defined( baseEvent_defined_ )
#define baseEvent_defined_

#include "json/json.h"
#include "utils/object.h"
#include "utils/unixSocket.h"
#include <stdlib.h>

typedef std::map<std::string,std::string> baseEventSysMapT;
typedef baseEventSysMapT::iterator baseEventSysMapIteratorT;
typedef std::pair<std::string,std::string> baseEventSysMapPairT;

typedef std::pair<std::string,std::string> baseEventExecPairT;
typedef std::vector<baseEventExecPairT> baseEventExecVectorT;
typedef baseEventExecVectorT::iterator baseEventExecVectorIteratorT;

class recoveryLog;

class baseEvent : public object
{
  // Definitions
  public:
    static const int FRAME_HEADER_LEN = 27; // strlen(FRAME_HEADER)+strlen(PROTOCOL_VERSION_NUMBER)+8 - the 8 is ':%06u\n'
    static const int BLOCK_HEADER_LEN = 39; // 04,1,000060,1,000080,1,000005,1,000000\n
    static const unsigned int MAX_HEADER_BLOCK_LEN = 999999; // corresponds to %06d
    static const char* PROTOCOL_VERSION_NUMBER;
    static const char* FRAME_HEADER;
    static const int MAX_RETRIES = 5;
    enum eEventType { EV_UNKNOWN=0,EV_BASE=1,EV_SCRIPT=2,EV_PERL=3,EV_BIN=4,EV_URL=5,EV_RESULT=6,EV_WORKER_DONE=7,EV_COMMAND=8,EV_REPLY=9,EV_ERROR=10 };
    enum eFdType { FD_SOCKET,FD_PIPE,FD_FILE };

    /**
     * Commands are always handled out of band and distributed to all workers if not handled 
     * directly by the nucleus
     * CMD_STATS=1
     * CMD_RESET_STATS=2
     * CMD_REOPEN_LOG=3
     * CMD_REREAD_CONF=4
     * CMD_EXIT_WHEN_DONE=5
     * CMD_SEND_UDP_PACKET=6
     * CMD_TIMER_SIGNAL=7
     * CMD_CHILD_SIGNAL=8
     * CMD_APP=9                - no longer used
     * CMD_SHUTDOWN=10
     * CMD_NUCLEUS_CONF=11      - configures the nucleus parameters
     * CMD_DUMP_STATE=12
     * CMD_NETWORKIF_CONF=13    - intended to configure the socket process
     * CMD_END_OF_QUEUE=14      - indicates the last event on the queue
     * CMD_MAIN_CONF=15         - reconfigure command intended for the main app (txDelta, telcoServe, yabrooNotify)
     * CMD_PERSISTENT_APP=16    - command forwarded to the persistent app on the appropriate queue
     * CMD_EVENT=17             - event that needs be processed
     * CMD_WORKER_CONF=18
     * */
    enum eCommandType { CMD_NONE=0,CMD_STATS=1,CMD_RESET_STATS=2,CMD_REOPEN_LOG=3,CMD_REREAD_CONF=4,CMD_EXIT_WHEN_DONE=5,CMD_SEND_UDP_PACKET=6,CMD_TIMER_SIGNAL=7,CMD_CHILD_SIGNAL=8,CMD_APP=9,CMD_SHUTDOWN=10,CMD_NUCLEUS_CONF=11,CMD_DUMP_STATE=12,CMD_NETWORKIF_CONF=13,CMD_END_OF_QUEUE=14,CMD_MAIN_CONF=15,CMD_PERSISTENT_APP=16,CMD_EVENT=17,CMD_WORKER_CONF=18 };
 
    // Methods
  public:
    baseEvent( );
    baseEvent( eEventType type, const char* queue=NULL, const char* name="baseEvent" );
    baseEvent( const char* body, const char* name="baseEvent" );
    baseEvent( const baseEvent& c );
    virtual ~baseEvent();
    virtual std::string toString ();

    std::string toStringBrief( );
    const char* typeToString( );
    const char* commandToString( );

    // part1 properties are always extracted
    // part1["eventType"] = (int)eventType;
    // part1["reference"] = reference;
    // part1["returnFd"] = returnFd;
    // part1["destQueue"] = destQueue;
    eEventType getType( )                                   {return eventType;}
    void setType( eEventType t )                            {eventType=t;log.setInstanceName(typeToString());bPart1JsonValid=false;}
    std::string& getRef( )                                  {return reference;}
    unsigned long getRefAsLong( )                           {return strtoul(reference.c_str(),NULL,10);}
    void setRef( const std::string& theRef )                {reference=theRef;bPart1JsonValid=false;}
    void setRef( unsigned long theRef )                     {char v[40];sprintf(v,"%lu",theRef);reference=v;bPart1JsonValid=false;}
    std::string& generateRef( )                             {long randVal=random(); char v[40]; sprintf(v,"%05lu-%05lu",(randVal>>16)&0xffff,randVal&0xffff );std::string str(v);setRef(str);return reference;}
    int getReturnFd( )                                      {return atoi(returnFd.c_str());}
    void getReturnFdRef( std::string& ref );
    void* getReturnFdRefAsVoidP( );
    void addReturnRouting( int returnFd, void* ref );
    void addReturnRouting( int returnFd, const char* ref );
    void shiftReturnFd( )                                   {std::string::size_type t; if( (t=returnFd.find(':'))!=std::string::npos) returnFd.erase(0,t+1);bPart1JsonValid=false;}  // equivalent of Perl shift operator - drop the first fd in line along with the record separator
    const char* getFullReturnFd( )                          {return returnFd.c_str();}
    void setReturnFd( const char* theReturnFd )             {returnFd=theReturnFd;bPart1JsonValid=false;}
    std::string& getDestQueue( )                            {if(mainQueue.empty()) parseMainDestQueue(); return mainQueue;}
    unsigned int getSubQueue( )                             {if(mainQueue.empty()) parseMainDestQueue(); return subQueue;}
    void setDestQueue( const std::string& s )               {destQueue=s;bPart1JsonValid=false;}
    void setDestQueue( const char* s )                      {destQueue=s;bPart1JsonValid=false;}
    std::string& getFullDestQueue( )                        {return destQueue;}
    void parseMainDestQueue( );

    // part2 properties extracted on request - all the accessor methods can throw
    // part2["trace"] = trace;
    // part2["traceTimestamp"] = traceTimestamp;
    // part2["expiryTime"] = expiryTime;
    // part2["lifetime"] = lifetime;
    // part2["retries"] = retries;
    // part2["wpid"] = workerPid;
    void setTrace( const std::string& t )                   {if(!bPart2Extracted)parsePart2();trace=t;bPart2JsonValid=false;}
    std::string& getTrace( )                                {if(!bPart2Extracted)parsePart2();return trace;}
    void appendTrace( const char* t )                       {if(!bPart2Extracted)parsePart2();trace.append(t);bPart2JsonValid=false;}
    void appendTrace( std::string& t )                      {if(!bPart2Extracted)parsePart2();trace.append(t);bPart2JsonValid=false;}
    void setTraceTimestamp( const std::string& t )          {if(!bPart2Extracted)parsePart2();traceTimestamp=t;bPart2JsonValid=false;}
    void setTraceTimestamp( const char* t )                 {if(!bPart2Extracted)parsePart2();traceTimestamp=t;bPart2JsonValid=false;}
    std::string& getTraceTimestamp( )                       {if(!bPart2Extracted)parsePart2();return traceTimestamp;}
    unsigned int getExpiryTime( )                           {if(!bPart2Extracted)parsePart2();return expiryTime;}
    void setExpiryTime( unsigned int t )                    {if(!bPart2Extracted)parsePart2();expiryTime=t;bPart2JsonValid=false;}
    int  getLifetime( )                                     {if(!bPart2Extracted)parsePart2();return lifetime;}
    void setLifetime( int theTime )                         {if(!bPart2Extracted)parsePart2();lifetime=theTime;bPart2JsonValid=false;}
    void incRetryCounter( )                                 {if(!bPart2Extracted)parsePart2();retries++;bPart2JsonValid=false;}
    bool isRetryExceeded( )                                 {if(!bPart2Extracted)parsePart2();return retries>MAX_RETRIES;}
    int  getWorkerPid( )                                    {if(!bPart2Extracted)parsePart2();return workerPid;}
    void setWorkerPid( int thePid )                         {if(!bPart2Extracted)parsePart2();workerPid=thePid;bPart2JsonValid=false;}

    // sysParams
    // bStandardResponse,command,url,scriptName,result,bSuccess,bExpectReply,errorString,
    // failureCause,systemParam,elapsedTime,bGeneratedRecoveryEvent,
    bool getStandardResponse( )                             {if(!bSysParamsExtracted)parseSysParams();if(!sysParams.isMember("bStandardResponse"))return false;Json::Value v=sysParams.get("bStandardResponse",true);if(v.isInt())return (v.asInt()==0)?false:true;else{log.warn(log.LOGMOSTLY,"getStandardResponse:not boolean:'%s'",v.toStyledString().c_str());return false;}}
    void setStandardResponse( bool b )                      {if(!bSysParamsExtracted)parseSysParams();sysParams["bStandardResponse"]=b;bSysParamJsonValid=false;}
    enum eCommandType getCommand( )                         {if(!bSysParamsExtracted)parseSysParams();if(!sysParams.isMember("command"))return CMD_NONE;Json::Value v=sysParams.get("command",(int)CMD_NONE);if(v.isInt())return (eCommandType)v.asInt();else{log.warn(log.LOGMOSTLY,"getCommand:not integer:'%s'",v.toStyledString().c_str());return CMD_NONE;}}
    void setCommand( enum eCommandType e )                  {if(!bSysParamsExtracted)parseSysParams();sysParams["command"]=(int)e;bSysParamJsonValid=false;}
    std::string getUrl( )                                   {if(!bSysParamsExtracted)parseSysParams();if(!sysParams.isMember("url"))return std::string();return sysParams.get("url",Json::Value()).asString();}
    void setUrl( const std::string& str )                   {if(!bSysParamsExtracted)parseSysParams();sysParams["url"]=str;bSysParamJsonValid=false;}
    std::string getScriptName( )                            {if(!bSysParamsExtracted)parseSysParams();if(!sysParams.isMember("scriptName"))return std::string();return sysParams.get("scriptName",Json::Value()).asString();}
    void setScriptName( const std::string& str )            {if(!bSysParamsExtracted)parseSysParams();sysParams["scriptName"]=str;bSysParamJsonValid=false;}
    std::string getResult( )                                {if(!bSysParamsExtracted)parseSysParams();if(!sysParams.isMember("result"))return std::string();return sysParams.get("result",Json::Value()).asString();}
    void setResult( const std::string& str )                {if(!bSysParamsExtracted)parseSysParams();sysParams["result"]=str;bSysParamJsonValid=false;}
    void setResult( const char* str )                       {if(!bSysParamsExtracted)parseSysParams();sysParams["result"]=str;bSysParamJsonValid=false;}
    bool isSuccess( )                                       {if(!bSysParamsExtracted)parseSysParams();if(!sysParams.isMember("bSuccess"))return false;Json::Value v=sysParams.get("bSuccess",true);if(v.isInt())return (v.asInt()==0)?false:true;else{log.warn(log.LOGMOSTLY,"isSuccess:not boolean:'%s'",v.toStyledString().c_str());return false;}}
    void setSuccess( bool b )                               {if(!bSysParamsExtracted)parseSysParams();sysParams["bSuccess"]=(int)b;bSysParamJsonValid=false;}
    bool getExpectReply( )                                  {if(!bSysParamsExtracted)parseSysParams();if(!sysParams.isMember("bExpectReply"))return false;Json::Value v=sysParams.get("bExpectReply",true);if(v.isInt())return (v.asInt()==0)?false:true;else{log.warn(log.LOGMOSTLY,"getExpectReply:not boolean:'%s'",v.toStyledString().c_str());return false;}}
    void setExpectReply( bool b )                           {if(!bSysParamsExtracted)parseSysParams();sysParams["bExpectReply"]=b;bSysParamJsonValid=false;}
    std::string getErrorString( )                           {if(!bSysParamsExtracted)parseSysParams();if(!sysParams.isMember("errorString"))return std::string();return sysParams.get("errorString",Json::Value()).asString();}
    void setErrorString( const std::string& str )           {if(!bSysParamsExtracted)parseSysParams();sysParams["errorString"]=str;bSysParamJsonValid=false;}
    std::string getFailureCause( )                          {if(!bSysParamsExtracted)parseSysParams();if(!sysParams.isMember("failureCause"))return std::string();return sysParams.get("failureCause",Json::Value()).asString();}
    void setFailureCause( const std::string& str )          {if(!bSysParamsExtracted)parseSysParams();sysParams["failureCause"]=str;bSysParamJsonValid=false;}
    std::string getSystemParam( )                           {if(!bSysParamsExtracted)parseSysParams();if(!sysParams.isMember("systemParam"))return std::string();return sysParams.get("systemParam",Json::Value()).asString();}
    void setSystemParam( const std::string& str )           {if(!bSysParamsExtracted)parseSysParams();sysParams["systemParam"]=str;bSysParamJsonValid=false;}
    void setElapsedTime( unsigned int theTime )             {if(!bSysParamsExtracted)parseSysParams();sysParams["elapsedTime"]=theTime;bSysParamJsonValid=false;}
    unsigned int getElapsedTime( )                          {if(!bSysParamsExtracted)parseSysParams();if(!sysParams.isMember("bStandardResponse"))return 0;Json::Value v=sysParams.get("bStandardResponse",0);if(v.isUInt())return v.asUInt();else{log.warn(log.LOGMOSTLY,"getElapsedTime:not unsigned int:'%s'",v.toStyledString().c_str());return 0;}}
    void setRecoveryEvent( bool b )                         {if(!bSysParamsExtracted)parseSysParams();sysParams["bGeneratedRecoveryEvent"]=b;bSysParamJsonValid=false;}
    bool getRecoveryEvent( )                                {if(!bSysParamsExtracted)parseSysParams();if(!sysParams.isMember("bGeneratedRecoveryEvent"))return false;Json::Value v=sysParams.get("bGeneratedRecoveryEvent",true);if(v.isInt())return (v.asInt()==0)?false:true;else{log.warn(log.LOGMOSTLY,"getRecoveryEvent:not boolean:'%s'",v.toStyledString().c_str());return false;}}

    // execParams
    // named parameters
    // addParam will replace if the key already exists
    void addParam( const char* name, const std::string& value )       {if(!bExecParamsExtracted)parseExecParams();execParams[name]=value;bExecParamJsonValid=false;}
    void addParam( const char* name, const char* value )              {if(!bExecParamsExtracted)parseExecParams();execParams[name]=value;bExecParamJsonValid=false;}
    void addParam( const char* name, unsigned value )                 {if(!bExecParamsExtracted)parseExecParams();execParams[name]=value;bExecParamJsonValid=false;}
    void addParam( const char* name, int value )                      {if(!bExecParamsExtracted)parseExecParams();execParams[name]=value;bExecParamJsonValid=false;}
    void addParam( const char* name, float value )                    {if(!bExecParamsExtracted)parseExecParams();execParams[name]=value;bExecParamJsonValid=false;}
    void addParamAsStr( const char* name, unsigned long value )       {if(!bExecParamsExtracted)parseExecParams();char s[64];sprintf(s,"%lu",value);execParams[name]=s;bExecParamJsonValid=false;}
    void addParamAsStr( const char* name, unsigned value )            {if(!bExecParamsExtracted)parseExecParams();char s[64];sprintf(s,"%u",value);execParams[name]=s;bExecParamJsonValid=false;}
    void addParamAsStr( const char* name, long value )                {if(!bExecParamsExtracted)parseExecParams();char s[64];sprintf(s,"%ld",value);execParams[name]=s;bExecParamJsonValid=false;}
    void addParamAsStr( const char* name, int value )                 {if(!bExecParamsExtracted)parseExecParams();char s[64];sprintf(s,"%d",value);execParams[name]=s;bExecParamJsonValid=false;}
    std::string getParam( const char* name )                          {if(!bExecParamsExtracted)parseExecParams();if(!execParams.isMember(name))return std::string();Json::Value v=execParams.get(name,Json::Value());if(v.isString())return v.asString();else throw Exception(log,log.WARN,"getParam: unable to convert to string name:'%s' val:'%s'",name,v.toStyledString().c_str());}
    bool getParam( const char* name, std::string& value )             {if(!bExecParamsExtracted)parseExecParams();if(!execParams.isMember(name))return false;Json::Value v=execParams.get(name,Json::Value());if(v.isString()){value=v.asString();return true;}else throw Exception(log,log.WARN,"getParam: unable to convert to string name:'%s' val:'%s'",name,v.toStyledString().c_str());}
    int  getParamAsInt( const char* name )                            {if(!bExecParamsExtracted)parseExecParams();if(!execParams.isMember(name))return 0;Json::Value v=execParams.get(name,Json::Value());if(v.isInt())return v.asInt();if(v.isUInt())return v.asUInt();else throw Exception(log,log.WARN,"getParam: unable to convert to int name:'%s' val:'%s'",name,v.toStyledString().c_str());}
    unsigned int  getParamAsUInt( const char* name )                  {if(!bExecParamsExtracted)parseExecParams();if(!execParams.isMember(name))return 0;Json::Value v=execParams.get(name,Json::Value());if(v.isUInt())return v.asUInt();if(v.isInt())return v.asInt();else throw Exception(log,log.WARN,"getParam: unable to convert to uint name:'%s' val:'%s'",name,v.toStyledString().c_str());}
    bool existsParam( const char* name )                              {if(!bExecParamsExtracted)parseExecParams();return execParams.isMember(name);}
    void deleteParam( const char* name )                              {if(!bExecParamsExtracted)parseExecParams();execParams.removeMember(name);}
    Json::ValueConstIterator paramBegin( )                            {if(!bExecParamsExtracted)parseExecParams();return execParamsConst.begin();}
    Json::ValueConstIterator paramEnd( )                              {if(!bExecParamsExtracted)parseExecParams();return execParamsConst.end();}

    // positional or script parameters
    void addScriptParam( const std::string str )                      {if(!bExecParamsExtracted)parseExecParams();execParams.append(str);bExecParamJsonValid=false;}
    void addScriptParam( unsigned s )                                 {if(!bExecParamsExtracted)parseExecParams();execParams.append(s);bExecParamJsonValid=false;}
    void addScriptParam( int s )                                      {if(!bExecParamsExtracted)parseExecParams();execParams.append(s);bExecParamJsonValid=false;}
    void addScriptParam( float s )                                    {if(!bExecParamsExtracted)parseExecParams();execParams.append(s);bExecParamJsonValid=false;}
    unsigned int scriptParamSize( )                                   {if(!bExecParamsExtracted)parseExecParams();return execParams.size();}
    std::string getScriptParam( int i )                               {if(!bExecParamsExtracted)parseExecParams();if(!execParams.isValidIndex(i))return std::string();if(execParams[i].isString())return execParams[i].asString();else throw Exception(log,log.WARN,"getScriptParam:parameters have to be strings at index:%d :'%s'",i,toString().c_str());}
    const char* getScriptParamAsCStr( int i )                         {if(!bExecParamsExtracted)parseExecParams();if(!execParams.isValidIndex(i))return NULL;if(execParams[i].isString())return execParams[i].asCString();else throw Exception(log,log.WARN,"getScriptParam:C parameters have to be strings at index:%d :'%s'",i,toString().c_str());}
    int getScriptParamAsInt( int i )                                  {if(!bExecParamsExtracted)parseExecParams();if(!execParams.isValidIndex(i))throw new Exception(log,log.WARN,"getScriptParamAsInt:invalid index:%d size:%d",i,execParams.size());if(execParams[i].isInt())return execParams[i].asInt();if(execParams[i].isConvertibleTo(Json::intValue))return execParams[i].asInt();else throw new Exception(log,log.WARN,"getScriptParamAsInt:not int at index:%d",i);}
    unsigned getScriptParamAsUInt( int i )                            {if(!bExecParamsExtracted)parseExecParams();if(!execParams.isValidIndex(i))throw new Exception(log,log.WARN,"getScriptParamAsUInt:invalid index:%d size:%d",i,execParams.size());if(execParams[i].isUInt())return execParams[i].asUInt();if(execParams[i].isConvertibleTo(Json::uintValue))return execParams[i].asUInt();else throw new Exception(log,log.WARN,"getScriptParamAsUInt:not unsigned at index:%d",i);}
  
    // non-archived properties
    bool isExpired( unsigned int now=time(NULL) );
    bool hasBeenExpired()                                             {return bExpired;}
    void expire()                                                     {bExpired=true;}
    unsigned int getQueueTime( )                                      {return queueTime;}
    void setQueueTime( unsigned int t )                               {queueTime=t;}
    unsigned int getReadyTime( )                                      {return readyTime;}
    void setReadyTime( unsigned int s )                               {readyTime = s;}

    // serialisation support
    virtual int serialise( int fd, eFdType fdType=FD_SOCKET );
    virtual std::string& serialiseToString( );
    std::string& getStrSerialised( )                                  {return strSerialised;}
    int serialiseNonBlock( int fd, eFdType fdType=FD_SOCKET );
    int serialiseToFile( int fd );
    static baseEvent* unSerialise( unixSocket *fd );
    static baseEvent* unSerialiseFromFile( const char* fn );
    static baseEvent* unSerialiseFromString( const std::string& packet );

  private:
    void parseBody( const char* body );
    void serialisePart1( );
    void parsePart1( );
    void serialisePart2( );
    void parsePart2( );
    void serialiseSysParam( );
    void parseSysParams( );
    void serialiseExecParam( );
    void parseExecParams( );

    // Properties
  public:
    static recoveryLog*             theRecoveryLog;      ///< the recovery log
    static logger                   staticLogger;        ///< class scope logger

  protected:
    std::string                     strSerialised;        ///< string version of object serialisation

  private:
    bool                            bPart1Extracted;      ///< part 1 extracted from json
    bool                            bPart1JsonValid;      ///< true if jsonPart1 is a valid representation - ie the values have not changed
    std::string                     jsonPart1;            ///< json representation of part1
    eEventType                      eventType;            ///< event sub-type
    std::string                     reference;            ///< reference for processing of the result
    std::string                     returnFd;             ///< return file descriptor - ':' separated list of fileIds; ';' adds a reference to a filedescriptor; if -1 no result should be returned; typical use would be socketProcessFileId:tcpId;unixSocketPointer to return a response back to a client; on submission via a stream socket to the socket process a 0 in this field requests a persistent reply channel
    std::string                     destQueue;            ///< queue that is to service the event - can be followed by a numeric sub-queue specifier separated by a ';'

    bool                            bPart2Extracted;      ///< part 2 extracted from json
    bool                            bPart2JsonValid;      ///< true if jsonPart2 is a valid representation - ie the values have not changed
    std::string                     jsonPart2;            ///< json representation of part2
    std::string                     trace;                ///< logging trace - typically a history
    std::string                     traceTimestamp;       ///< the trace timestamp to be used for subsequent logging entries
    unsigned int                    expiryTime;           ///< absolute expiry time in seconds since Jan 1970 or 0
    int                             lifetime;             ///< requested lifetime of the object in seconds - -1 if not applicable
    int                             retries;              ///< number of retries to process
    int                             workerPid;            ///< worker pid - in the case where the event is destined for a particular worker in the pool

    bool                            bSysParamsExtracted;  ///< true if the sysParams have been extracted
    bool                            bSysParamJsonValid;   ///< true if jsonSysParams is a valid representation - ie the values have not changed
    std::string                     jsonSysParams;        ///< json representation of the sysParams
    Json::Value                     sysParams;            ///< system params not user/execution params
    bool                            bExecParamsExtracted; ///< true if execParams have been extracted
    bool                            bExecParamJsonValid;  ///< true if jsonExecParams is a valid representation - ie the values have not changed
    std::string                     jsonExecParams;       ///< json representation of the execParams
    Json::Value                     execParams;           ///< parameters used for execution of scripts/urls; contains the result pairs on return
    const Json::Value&              execParamsConst;

    // non archived properties
    unsigned int                    queueTime;            ///< time at which the event was placed in the queue - to be used to calculate queuing times
    unsigned int                    readyTime;            ///< time at which the object will be ready for execution - only used by the delay queue - 0 means it is ready for immediate execution, on submission this represents an offset to the current time on the server
    bool                            bExpired;             ///< event is expired and has been handled as such
    std::string                     mainQueue;            ///< main queue name seperated out of destQueue
    unsigned int                    subQueue;             ///< sub queue id seperated out of destQueue
};	// class baseEvent

  
#endif // !defined( baseEvent_defined_)
