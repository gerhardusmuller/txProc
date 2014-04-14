// appBase is the base class for a txProc persistent application
// 
// Versioning: a.b.c a is a major release, b represents changes or new features, c represents bug fixes. 
// @version 1.0.0   04/04/2014    Gerhardus Muller     Script created
//
// @note extraction of part2 et al is handled differently to the C code; panics on attempting to use part2 et al without extraction
//
// @todo - the json unmarshalling should most likely be a bit more robust allowing at least auto conversions for bool and number formats
// 
// @bug
//
// Copyright Gerhardus Muller

package baseEvent

import (
  "fmt"
  "net"
  "errors"
  "io/ioutil"
  "encoding/json"
  "strconv"
  "strings"
  "reflect"
  "github.com/gerhardusmuller/txProc/go/utils"
  )

type EEventType int
type ECommandType int
type part1Type struct {
  EventType               EEventType    `json:"eventType"`
  Reference               string        `json:"reference"`
  ReturnFd                string        `json:"returnFd,omitempty"`           // -1 is default that is empty; 0 indicates we are expecting a reply
  DestQueue               string        `json:"destQueue,omitempty"`
} // part1Type
type part2Type struct {
  Trace                   string        `json:"trace,omitempty"`
  TraceTimestamp          string        `json:"traceTimestamp,omitempty"`
  ExpiryTime              uint          `json:"expiryTime,omitempty"`
  Lifetime                int           `json:"lifetime,omitempty"`
  Retries                 int           `json:"retries,omitempty"`
  Wpid                    int           `json:"wpid,omitempty"`
} // part2Type
type sysParamsType struct {
  BStandardResponse       bool          `json:"bStandardResponse,omitempty"`  // request parsing of old style text responses
  Command                 ECommandType  `json:"command,omitempty"`
  Url                     string        `json:"url,omitempty"`
  ScriptName              string        `json:"scriptName,omitempty"`
  Result                  string        `json:"result,omitempty"`
  BSuccess                int           `json:"bSuccess"`                     // the int flavour is history
  BExpectReply            bool          `json:"bExpectReply,omitempty"`       // set to true if the server thinks we should wait for response on a streaming socket
  ErrorString             string        `json:"errorString,omitempty"`
  FailureCause            string        `json:"failureCause,omitempty"`
  SystemParam             string        `json:"systemParam,omitempty"`
  ElapsedTime             uint          `json:"elapsedTime,omitempty"`
  BGeneratedRecoveryEvent bool          `json:"bGeneratedRecoveryEvent,omitempty"`
} // sysParamsType
type BaseEvent struct {
  part1Json               []byte
  part1                   part1Type

  part2JsonExtracted      bool                    // needs to be set on an new empty object
  part2Json               []byte
  part2                   part2Type

  sysParamsExtracted      bool                    // needs to be set on an new empty object
  sysParamsJson           []byte
  sysParams               sysParamsType

  execParamsExtracted     bool                    // needs to be set on an new empty object
  execParamsJson          []byte
  execParams              map[string]string
  execArray               []string

  verbose                 bool
  errString               string
} // type baseEvent struct

const FRAME_HEADER = "#frameNewframe#v"
const PROTOCOL_VERSION_NUMBER = "3.0"
const FRAME_HEADER_LEN = 27                 // strlen(FRAME_HEADER)+strlen(PROTOCOL_VERSION_NUMBER)+8 - the 8 is ':%06u\n'
const BLOCK_HEADER_LEN = 39
const READ_BUF_SIZE = 32768
const MAX_HEADER_BLOCK_LEN = 999999         // corresponds to %06d

// EEventType values
// EV_ enumeration
const ( 
  EV_UNKNOWN      = 0
  EV_BASE         = 1
  EV_SCRIPT       = 2
  EV_PERL         = 3
  EV_BIN          = 4
  EV_URL          = 5
  EV_RESULT       = 6
  EV_WORKER_DONE  = 7
  EV_COMMAND      = 8
  EV_REPLY        = 9
  EV_ERROR        = 10
  )

// ECommandType  values
// CMD_ enumeration
const (
  CMD_NONE            = 0
  CMD_STATS           = 1
  CMD_RESET_STATS     = 2
  CMD_REOPEN_LOG      = 3
  CMD_REREAD_CONF     = 4
  CMD_EXIT_WHEN_DONE  = 5
  CMD_SEND_UDP_PACKET = 6
  CMD_TIMER_SIGNAL    = 7
  CMD_CHILD_SIGNAL    = 8
  CMD_APP             = 9
  CMD_SHUTDOWN        = 10
  CMD_NUCLEUS_CONF    = 11
  CMD_DUMP_STATE      = 12
  CMD_NETWORKIF_CONF  = 13
  CMD_END_OF_QUEUE    = 14
  CMD_MAIN_CONF       = 15
  CMD_PERSISTENT_APP  = 16
  CMD_EVENT           = 17
  CMD_WORKER_CONF     = 18
  )

// SerialiseToSocket return values
const (
  RESULT_ERR          = 0             // err should be set also
  RESULT_OK           = 1             // accepted by txProc - no reply event
  RESULT_RESPONSE     = 2             // read a response object from the socket
  )

/**
 * file global verbose flag
 * **/
var Verbose bool

/**
 * object verbose flag
 * **/
func (s *BaseEvent) Verbose() bool {
  return s.verbose
} // func Verbose
func (s *BaseEvent) SetVerbose( v bool ) {
  s.verbose = v;
} // func SetVerbose

/**
 * part1 properties eventType,reference,returnFd,destQueue
 * **/

func (s *BaseEvent) serialisePart1() error {
  var err error
  if s.part1Json,err = json.Marshal(s.part1); err != nil {
    s.errString = "serialisePart1 error: " + err.Error()
    return s;
  } // if
  return nil;
} // BaseEvent serialisePart1

func (s *BaseEvent) parsePart1() error {
  if err := json.Unmarshal(s.part1Json, &s.part1); err != nil {
    s.errString = "parsePart1 error: " + err.Error()
    return s;
  } // if
  return nil;
} // BaseEvent parsePart1
 
/**
 * eventType
 * **/
func (s *BaseEvent) EventType() EEventType {
  return s.part1.EventType
} // BaseEvent EventType
func (s *BaseEvent) SetEventType( e EEventType ) error {
  if err := e.Validate(); err != nil {
    return err
  } // if
  s.part1.EventType = e
  return nil
} // BaseEvent SetEventType

/**
 * reference
 * **/
func (s *BaseEvent) Reference() string {
  return s.part1.Reference
} // BaseEvent Reference
func (s *BaseEvent) SetReference( ref string ) error {
  s.part1.Reference = ref
  return nil
} // BaseEvent SetReference

/**
 * returnFd - not a file descriptor - rather a routing string
 * set to "0" to indicate that we will be waiting on a stream socket for a reply
 * **/
func (s *BaseEvent) ReturnFd() string {
  return s.part1.ReturnFd
} // BaseEvent ReturnFd
func (s *BaseEvent) SetReturnFd( fd string ) error {
  s.part1.ReturnFd = fd
  return nil
} // BaseEvent SetReturnFd

/**
 * destQueue
 * **/
func (s *BaseEvent) DestQueue() string {
  return s.part1.DestQueue
} // BaseEvent DestQueue
func (s *BaseEvent) SetDestQueue( q string ) error {
  s.part1.DestQueue = q
  return nil
} // BaseEvent SetDestQueue

/**
 * part2 properties - trace,traceTimestamp,expiryTime,lifetime,retries,workerPid(wpid)
 ** */

func (s *BaseEvent) serialisePart2() error {
  if !s.part2JsonExtracted { return nil }                     // we assume the existing serialised json is valid
  var err error
  if s.part2Json,err = json.Marshal(s.part2); err != nil {
    s.errString = "serialisePart2 error: " + err.Error()
    return s;
  } // if
  return nil;
} // BaseEvent serialisePart2

func (s *BaseEvent) parsePart2() error {
  if err := json.Unmarshal(s.part2Json, &s.part2); err != nil {
    s.errString = "parsePart2 error: " + err.Error()
    return s;
  } // if
  s.part2JsonExtracted = true
  return nil;
} // BaseEvent parsePart2
 
/**
 * trace
 * **/
func (s *BaseEvent) Trace() string {
  if !s.part2JsonExtracted { panic("!s.part2JsonExtracted"); } // if
  return s.part2.Trace
} // BaseEvent Trace
func (s *BaseEvent) SetTrace( t string ) error {
  s.part2.Trace = t
  return nil
} // BaseEvent SetTrace

/**
 * traceTimestamp
 * **/
func (s *BaseEvent) TraceTimestamp() string {
  if !s.part2JsonExtracted { panic("!s.part2JsonExtracted"); } // if
  return s.part2.TraceTimestamp
} // BaseEvent TraceTimestamp
func (s *BaseEvent) SetTraceTimestamp( t string ) error {
  s.part2.TraceTimestamp = t
  return nil
} // BaseEvent SetTraceTimestamp

/**
 * expiryTime
 * **/
func (s *BaseEvent) ExpiryTime() uint {
  if !s.part2JsonExtracted { panic("!s.part2JsonExtracted"); } // if
  return s.part2.ExpiryTime
} // BaseEvent ExpiryTime
func (s *BaseEvent) SetExpiryTime( t uint ) error {
  s.part2.ExpiryTime = t
  return nil
} // BaseEvent SetExpiryTime

/**
 * lifetime
 * **/
func (s *BaseEvent) Lifetime() int {
  if !s.part2JsonExtracted { panic("!s.part2JsonExtracted"); } // if
  return s.part2.Lifetime
} // BaseEvent Lifetime
func (s *BaseEvent) SetLifetime( t int ) error {
  s.part2.Lifetime = t
  return nil
} // BaseEvent SetLifetime

/**
 * retries
 * **/
func (s *BaseEvent) Retries() int {
  if !s.part2JsonExtracted { panic("!s.part2JsonExtracted"); } // if
  return s.part2.Retries
} // BaseEvent Retries
func (s *BaseEvent) SetRetries( t int ) error {
  s.part2.Retries = t
  return nil
} // BaseEvent SetRetries

/**
 * wpid - workerPid
 * **/
func (s *BaseEvent) Wpid() int {
  if !s.part2JsonExtracted { panic("!s.part2JsonExtracted"); } // if
  return s.part2.Wpid
} // BaseEvent Wpid
func (s *BaseEvent) SetWpid( t int ) error {
  s.part2.Wpid = t
  return nil
} // BaseEvent SetWpid

/**
 * sysParams - bStandardResponse,command,url,scriptName,result,bSuccess,bExpectReply,
 * errorString,failureCause,systemParam,elapsedTime,bGeneratedRecoveryEvent
 ** */

func (s *BaseEvent) serialiseSysParams() error {
  if !s.sysParamsExtracted { return nil }                             // we assume the existing serialised json is valid
  var err error
  if s.sysParamsJson,err = json.Marshal(s.sysParams); err != nil {
    s.errString = "serialiseSysParams error: " + err.Error()
    return s;
  } // if
  return nil;
} // BaseEvent serialiseSysParams

func (s *BaseEvent) parseSysParams() error {
  if err := json.Unmarshal(s.sysParamsJson, &s.sysParams); err != nil {
    s.errString = "parseSysParams error: " + err.Error()
    return s;
  } // if
  s.sysParamsExtracted = true
  return nil;
} // BaseEvent parseSysParams
 
/**
 * bStandardResponse
 * **/
func (s *BaseEvent) BStandardResponse() bool {
  if !s.sysParamsExtracted { panic("!s.sysParamsExtracted"); }
  return s.sysParams.BStandardResponse
} // BaseEvent BStandardResponse
func (s *BaseEvent) SetBStandardResponse( t bool ) error {
  s.sysParams.BStandardResponse = t
  return nil
} // BaseEvent SetBStandardResponse

/**
 * command
 * **/
func (s *BaseEvent) Command() ECommandType {
  if !s.sysParamsExtracted { panic("!s.sysParamsExtracted"); }
  return s.sysParams.Command
} // BaseEvent Command
func (s *BaseEvent) SetCommand( t ECommandType ) error {
  if err := t.Validate(); err != nil {
    return err
  } // if
  s.sysParams.Command = t
  return nil
} // BaseEvent SetCommand

/**
 * url
 * **/
func (s *BaseEvent) Url() string {
  if !s.sysParamsExtracted { panic("!s.sysParamsExtracted"); }
  return s.sysParams.Url
} // BaseEvent Url
func (s *BaseEvent) SetUrl( t string ) error {
  s.sysParams.Url = t
  return nil
} // BaseEvent SetUrl

/**
 * scriptName
 * **/
func (s *BaseEvent) ScriptName() string {
  if !s.sysParamsExtracted { panic("!s.sysParamsExtracted"); }
  return s.sysParams.ScriptName
} // BaseEvent 
func (s *BaseEvent) SetScriptName( t string ) error {
  s.sysParams.ScriptName = t
  return nil
} // BaseEvent SetScriptName

/**
 * result
 * **/
func (s *BaseEvent) Result() string {
  if !s.sysParamsExtracted { panic("!s.sysParamsExtracted"); }
  return s.sysParams.Result
} // BaseEvent 
func (s *BaseEvent) SetResult( t string ) error {
  s.sysParams.Result = t
  return nil
} // BaseEvent SetResult

/**
 * bSuccess - legacy calls for bSuccess to be an integer
 * **/
func (s *BaseEvent) BSuccess() bool {
  if !s.sysParamsExtracted { panic("!s.sysParamsExtracted"); }
  return (s.sysParams.BSuccess == 1)
} // BaseEvent 
func (s *BaseEvent) SetBSuccess( t bool ) error {
  if t {
    s.sysParams.BSuccess = 1
  } else {
    s.sysParams.BSuccess = 0
  } // else
  return nil
} // BaseEvent SetBSuccess

/**
 * bExpectReply
 * **/
func (s *BaseEvent) BExpectReply() bool {
  if !s.sysParamsExtracted { panic("!s.sysParamsExtracted"); }
  return s.sysParams.BExpectReply
} // BaseEvent 
func (s *BaseEvent) SetBExpectReply( t bool ) error {
  s.sysParams.BExpectReply = t
  return nil
} // BaseEvent SetBExpectReply

/**
 * errorString
 * **/
func (s *BaseEvent) ErrorString() string {
  if !s.sysParamsExtracted { panic("!s.sysParamsExtracted"); }
  return s.sysParams.ErrorString
} // BaseEvent 
func (s *BaseEvent) SetErrorString( t string ) error {
  s.sysParams.ErrorString = t
  return nil
} // BaseEvent SetErrorString

/**
 * failureCause
 * **/
func (s *BaseEvent) FailureCause() string {
  if !s.sysParamsExtracted { panic("!s.sysParamsExtracted"); }
  return s.sysParams.FailureCause
} // BaseEvent 
func (s *BaseEvent) SetFailureCause( t string ) error {
  s.sysParams.FailureCause = t
  return nil
} // BaseEvent SetFailureCause

/**
 * systemParam
 * **/
func (s *BaseEvent) SystemParam() string {
  if !s.sysParamsExtracted { panic("!s.sysParamsExtracted"); }
  return s.sysParams.SystemParam
} // BaseEvent 
func (s *BaseEvent) SetSystemParam( t string ) error {
  s.sysParams.SystemParam = t
  return nil
} // BaseEvent SetSystemParam

/**
 * elapsedTime
 * **/
func (s *BaseEvent) ElapsedTime() uint {
  if !s.sysParamsExtracted { panic("!s.sysParamsExtracted"); }
  return s.sysParams.ElapsedTime
} // BaseEvent 
func (s *BaseEvent) SetElapsedTime( t uint ) error {
  s.sysParams.ElapsedTime = t
  return nil
} // BaseEvent SetElapsedTime

/**
 * bGeneratedRecoveryEvent
 * **/
func (s *BaseEvent) BGeneratedRecoveryEvent() bool {
  if !s.sysParamsExtracted { panic("!s.sysParamsExtracted"); }
  return s.sysParams.BGeneratedRecoveryEvent
} // BaseEvent 
func (s *BaseEvent) SetBGeneratedRecoveryEvent( t bool ) error {
  s.sysParams.BGeneratedRecoveryEvent = t
  return nil
} // BaseEvent SetBGeneratedRecoveryEvent

/**
 * execParams
 * positional take precendence over named parameters
 * **/

func (s *BaseEvent) serialiseExecParams() error {
  if !s.execParamsExtracted { return nil }            // we assume the existing serialised json is valid
  var err error
  if cap(s.execArray) > 0 {
    if s.execParamsJson,err = json.Marshal(s.execArray); err != nil {
      s.errString = "serialiseExecParams positional error: " + err.Error()
      return s;
    } // if
  } else if len(s.execParams) > 0 {
    if s.execParamsJson,err = json.Marshal(s.execParams); err != nil {
      s.errString = "serialiseExecParams named error: " + err.Error()
      return s;
    } // if
  } // else
  return nil;
} // BaseEvent serialiseExecParams

func (s *BaseEvent) parseExecParams() error {
  if len(s.execParamsJson) > 0 {
    if s.execParamsJson[0] == '[' {
      if err := json.Unmarshal(s.execParamsJson, &s.execArray); err != nil {
        s.errString = "parseExecParams positional error: " + err.Error()
        return s;
      } // if
    } else {
      if err := json.Unmarshal(s.execParamsJson, &s.execParams); err != nil {
        s.errString = "parseExecParams named error: " + err.Error()
        return s;
      } // if
    } // else
    s.execParamsExtracted = true
  } // if len(s.execParamsJson)
  return nil;
} // BaseEvent parseExecParams

/**
 * execParams
 * named parameters
 * **/
//void addParam( const char* name, unsigned value ) 
//void addParam( const char* name, int value )
//void addParam( const char* name, float value )
//void addParamAsStr( const char* name, unsigned long value )
//void addParamAsStr( const char* name, unsigned value )
//void addParamAsStr( const char* name, long value )
//void addParamAsStr( const char* name, int value )
func (s *BaseEvent) AddParamStr( name, value string ) error {
  if s.execParams==nil { s.execParams=make(map[string]string,10) }
  s.execParams[name] = value
  return nil
} // BaseEvent AddParam
func (s *BaseEvent) AddParamUintAsStr( name string, value uint ) error {
  if s.execParams==nil { s.execParams=make(map[string]string,10) }
  s.execParams[name] = fmt.Sprintf("%v",value)
  return nil
} // BaseEvent AddParam
func (s *BaseEvent) AddParamIntAsStr( name string, value int ) error {
  if s.execParams==nil { s.execParams=make(map[string]string,10) }
  s.execParams[name] = fmt.Sprintf("%v",value)
  return nil
} // BaseEvent AddParam
func (s *BaseEvent) AddParamFloatAsStr( name string, value float32 ) error {
  if s.execParams==nil { s.execParams=make(map[string]string,10) }
  s.execParams[name] = fmt.Sprintf("%v",value)
  return nil
} // BaseEvent AddParam

//std::string getParam( const char* name )
//int  getParamAsInt( const char* name )
//unsigned int  getParamAsUInt( const char* name )
func (s *BaseEvent) GetParamAsStr( name string ) (string,error) {
  if t,ok:=s.execParams[name]; ok {
    return t,nil
  } // if
  s.errString = fmt.Sprintf("GetParamAsStr execParams does not contain key:'%s'",name)
  return "",s
} // BaseEvent GetParamAsStr
func (s *BaseEvent) GetParamAsUint( name string ) (uint,error) {
  if t,ok:=s.execParams[name]; ok {
    v,err := strconv.ParseUint( t, 0, 0 )
    return uint(v),err
  } // if
  s.errString = fmt.Sprintf("GetParamAsUint execParams does not contain key:'%s'",name)
  return 9,s
} // BaseEvent GetParamAsUint
func (s *BaseEvent) GetParamAsInt( name string ) (int,error) {
  if t,ok:=s.execParams[name]; ok {
    v,err := strconv.ParseInt( t, 0, 0 )
    return int(v),err
  } // if
  s.errString = fmt.Sprintf("GetParamAsInt execParams does not contain key:'%s'",name)
  return 0,s
} // BaseEvent GetParamAsInt
func (s *BaseEvent) GetParamAsFloat( name string ) (float32,error) {
  if t,ok:=s.execParams[name]; ok {
    v,err := strconv.ParseFloat( t, 32 )
    return float32(v),err
  } // if
  s.errString = fmt.Sprintf("GetParamAsFloat execParams does not contain key:'%s'",name)
  return 0,s
} // BaseEvent GetParamAsFloat

//bool existsParam( const char* name )
//void deleteParam( const char* name )
func (s *BaseEvent) ExistsParam( name string ) (bool,error) {
  _,ok:=s.execParams[name]
  return ok,nil
} // BaseEvent ExistsParam
func (s *BaseEvent) DeleteParam( name string ) error {
  delete( s.execParams, name )
  return nil
} // BaseEvent DeleteParam

/**
 * execParams
 * positional parameters
 * **/

//void addScriptParam( const std::string str )
//void addScriptParam( unsigned s )
//void addScriptParam( int s )
//void addScriptParam( float s )
func (s *BaseEvent) AddScriptParamStr( value string ) error {
  if s.execArray==nil { s.execArray=make([]string,0,10) }
  s.execArray = append( s.execArray, value )
  return nil
} // BaseEvent AddScriptParamStr
func (s *BaseEvent) AddScriptParamUint( value uint ) error {
  if s.execArray==nil { s.execArray=make([]string,0,10) }
  s.execArray = append( s.execArray, fmt.Sprintf("%v",value) )
  return nil
} // BaseEvent AddScriptParamUint
func (s *BaseEvent) AddScriptParamInt( value int ) error {
  if s.execArray==nil { s.execArray=make([]string,0,10) }
  s.execArray = append( s.execArray, fmt.Sprintf("%v",value) )
  return nil
} // BaseEvent AddScriptParamInt
func (s *BaseEvent) AddScriptParamFloat( value float32 ) error {
  if s.execArray==nil { s.execArray=make([]string,0,10) }
  s.execArray = append( s.execArray, fmt.Sprintf("%v",value) )
  return nil
} // BaseEvent AddScriptParamIFloat

//unsigned int scriptParamSize( )
func (s *BaseEvent) ScriptParamSize() (int,error) {
  return len(s.execArray),nil
} // BaseEvent ExistsParam

//std::string getScriptParam( int i ) 
//const char* getScriptParamAsCStr( int i )
//int getScriptParamAsInt( int i )
//unsigned getScriptParamAsUInt( int i )
func (s *BaseEvent) GetScriptParamAsStr( i int ) (string,error) {
  if i < len(s.execArray) {
    return s.execArray[i],nil
  } // if
  s.errString = fmt.Sprintf("GetScriptParamAsStr i:%v > len", i, len(s.execArray) )
  return "",s
} // BaseEvent GetScriptParamAsStr
func (s *BaseEvent) GetScriptParamAsUint( i int ) (uint,error) {
  if i < len(s.execArray) {
    v,err := strconv.ParseUint( s.execArray[i], 0, 0 )
    return uint(v),err
  } // if
  s.errString = fmt.Sprintf("GetScriptParamAsUint i:%v > len", i, len(s.execArray) )
  return 0,s
} // BaseEvent GetScriptParamAsUint
func (s *BaseEvent) GetScriptParamAsInt( i int ) (int,error) {
  if i < len(s.execArray) {
    v,err := strconv.ParseInt( s.execArray[i], 0, 0 )
    return int(v),err
  } // if
  s.errString = fmt.Sprintf("GetScriptParamAsInt i:%v > len", i, len(s.execArray) )
  return 0,s
} // BaseEvent GetScriptParamAsInt
func (s *BaseEvent) GetScriptParamAsFloat( i int ) (float32,error) {
  if i < len(s.execArray) {
    v,err := strconv.ParseFloat( s.execArray[i], 32 )
    return float32(v),err
  } // if
  s.errString = fmt.Sprintf("GetScriptParamAsFloat i:%v > len", i, len(s.execArray) )
  return 0,s
} // BaseEvent GetScriptParamAsFloat

/**
 * serialises to a socket
 * retVal is 0 for an error, 1 for no response and 2 for a response - one of RESULT_ERR, RESULT_OK, RESULT_RESPONSE
 * **/
func (s *BaseEvent) SerialiseToSocket( conn net.Conn ) (int,error) {
  var retVal int
  // identify the type of conn - this should preferably be done comparing the types directly but how to compare
  // when we have pointer types ..
  bStreamSocket := false
  if reflect.TypeOf(conn).String()=="*net.TCPConn" { bStreamSocket = true }

  packet,err := s.SerialiseToString()
  if err != nil { return retVal,err }
  
  var numWritten int
  numWritten,err = conn.Write( []byte(packet) )
  if numWritten != len(packet) { appLogger.Log.Printf( "WARN SerialiseToSocket wrote %d of %d bytes", numWritten, len(packet) ) }
  appLogger.Log.Printf( "debug SerialiseToSocket wrote %d of %d bytes bStreamSocket:%v", numWritten, len(packet), bStreamSocket )
  if err != nil { return retVal,err }

  // read success or failure on submission and if we should wait for a response object
  // retVal is 0 for an error, 1 for no response and 2 for a response
  if( bStreamSocket ) {
    retVal,err = s.readReply( conn )
  } else {
    retVal = 1
  } // else bStreamSocket
  
  return retVal,err
} // SerialiseToSocket

/**
 * serialises to a file
 * **/
func (s *BaseEvent) SerialiseToFile( filename string ) error {
  packet,err := s.SerialiseToString()
  if err != nil { return err }
  
  err = ioutil.WriteFile( filename, []byte(packet), 0666 )
  return err
} // SerialiseToFile

/**
 * serialise the object to a string
 * create jsonPart1, jsonPart2, jsonSysParams, jsonExecParams
 * assume the jsonxx string to contain the correct json if it was never extracted
 * **/
func (s *BaseEvent) SerialiseToString() (string,error) {
  if err := s.serialisePart1(); err != nil { return "",err }
  if err := s.serialisePart2(); err != nil { return "",err }
  if err := s.serialiseSysParams(); err != nil { return "",err }
  if err := s.serialiseExecParams(); err != nil { return "",err }

  // construct the payload - 4 payloads all of type 1 which is a json payload
  // this can later be rewritten to make it possible for a derived class to add extra blocks
  payloadLen := BLOCK_HEADER_LEN+len(s.part1Json)+len(s.part2Json)+len(s.sysParamsJson)+len(s.execParamsJson)
  if payloadLen > MAX_HEADER_BLOCK_LEN {
    s.errString = fmt.Sprintf("SerialiseToString MAX_HEADER_BLOCK_LEN exceeded: payloadLen:%u, jsonPart1:%u, jsonPart2:%u, jsonSysParams:%u, jsonExecParams:%u",payloadLen,len(s.part1Json),len(s.part2Json),len(s.sysParamsJson),len(s.execParamsJson) )
    return "",s
  } // if
  payloadHeader := fmt.Sprintf( "%s%s:%06d\n%02d,1,%06d,1,%06d,1,%06d,1,%06d\n",FRAME_HEADER,PROTOCOL_VERSION_NUMBER,payloadLen,4,len(s.part1Json),len(s.part2Json),len(s.sysParamsJson),len(s.execParamsJson) )
//  strSerialised := payloadHeader + "\n" + string(s.part1Json) + "\n" + string(s.part2Json) + "\n" + string(s.sysParamsJson) + "\n" + string(s.execParamsJson)
  strSerialised := payloadHeader + string(s.part1Json) + string(s.part2Json) + string(s.sysParamsJson) + string(s.execParamsJson)

  appLogger.Log.Print( "BaseEvent SerialiseToString:", strSerialised )
  return strSerialised,nil;
} // BaseEvent serialiseToString

/**
 * parses a frame or message body
 * **/
func (s *BaseEvent) ParseBody( body []byte, parseAll bool ) error {
  s.part1.EventType = EV_UNKNOWN;
  var numSections,part1Size,part2Size,sysSize,execSize uint

  if numParams,err:=fmt.Sscanf(string(body), "%02d,1,%06d,1,%06d,1,%06d,1,%06d\n",&numSections,&part1Size,&part2Size,&sysSize,&execSize); numParams!=5 {
    s.errString = fmt.Sprintf( "ParseBody error: only parsed %d parameters from frame:'%s' - err:", numParams, body ) + err.Error()
    return s
  } // if
  if numSections != 4 {
    s.errString = fmt.Sprintf( "ParseBody error expected 4 sections found %d", numSections )
    return s
  } // if
  if part1Size == 0 {
    s.errString = "ParseBody error part1 cannot be empty"
    return s
  } // if

  startOffset := uint(BLOCK_HEADER_LEN);
  s.part1Json = body[startOffset:startOffset+part1Size]
  startOffset += part1Size
  if err:=s.parsePart1(); err != nil { return err }
  
  if part2Size > 0 {
    s.part2Json = body[startOffset:startOffset+part2Size]
    startOffset += part2Size
    if parseAll {
      if err:=s.parsePart2(); err != nil { return err }
      s.part2JsonExtracted = true
    } else {
      s.part2JsonExtracted = false
    } // else parseAll

  } // if
  if sysSize > 0 {
    s.sysParamsJson = body[startOffset:startOffset+sysSize]
    startOffset += sysSize
    if parseAll {
      if err:=s.parseSysParams(); err != nil { return err }
      s.sysParamsExtracted = true
    } else {
      s.sysParamsExtracted = false
    } // else parseAll
  } // if
  if execSize > 0 {
    s.execParamsJson = body[startOffset:startOffset+execSize]
    startOffset += execSize
    if parseAll {
      if err:=s.parseExecParams(); err != nil { return err }
      s.execParamsExtracted = true
    } else {
      s.execParamsExtracted = false
    } // else parseAll
  } // if

  appLogger.Log.Printf( "parseBody: sections:%d part1Size:%d part2Size:%d sysSize:%d execSize:%d",numSections,part1Size,part2Size,sysSize,execSize )
  return nil
} // BaseEvent parseBody

/**
 * network related
 **/

/**
 * read the socket greeting for socket streams
 * 032:txProc@tiferet pver 3.0 md 32768
 **/
func ReadGreeting( conn net.Conn ) error {
  // read the 032:
  headLenPiece := make( []byte, 4, 4 )
  numRead,err := conn.Read( headLenPiece )
  if err != nil { return errors.New( fmt.Sprintf( "ReadGreeting failed to read headLenPiece; read %d - %s", numRead, err ) ) }
  headLen,err := strconv.Atoi( string(headLenPiece[0:3]) )
  if err != nil { return errors.New( "ReadGreeting headLen:'" + string(headLenPiece) + "' could not be parsed - " + err.Error() ) }

  // read the txProc@tiferet pver 3.0 md 32768
  headPiece := make( []byte, headLen, headLen )
  numRead,err = conn.Read( headPiece )
  if err != nil { return errors.New( fmt.Sprintf( "ReadGreeting failed to read headPiece length:%d err:%s", headLen, err.Error() ) ) }

  // parse the protocol version
  index := strings.Index( string(headPiece), "pver " )
  if index == -1 { return errors.New( fmt.Sprintf( "ReadGreeting failed to pver from headPiece %s", headPiece ) ) }
  pver := headPiece[index+5:index+5+3]    // skip over 'pver ' and read 3.0

  // parse the max datagram length
  index = strings.Index( string(headPiece), "md " )
  if index == -1 { return errors.New( fmt.Sprintf( "ReadGreeting failed to md from headPiece %s", headPiece ) ) }
  md := headPiece[index+3:]               // skip over 'md ' and read the rest - typically 32768

  // verify the protocol version
  if string(pver) != "3.0" { return errors.New( fmt.Sprintf( "ReadGreeting only support pver 3.0 not %s", pver ) ) }
  appLogger.Log.Printf( "info ReadGreeting pver:%s md:%s", pver, md )

  return nil
} // BaseEvent ReadGreeting

/**
 * read stream based reply
 * @return (0,failReason) or (1,'') for no response or (2,'') to expect a response - one of RESULT_ERR, RESULT_OK, RESULT_RESPONSE
 * **/
func (s *BaseEvent) readReply( conn net.Conn ) (int,error) {
  reply,err := UnSerialiseFromSocket( conn, true )
  if err != nil { appLogger.Log.Print( "WARN readReply reply:", reply, " err:", err ) }
  if err != nil { return 0, err }

  retVal := 1
  if reply.BExpectReply() { retVal++ }
  return retVal,nil
} // BaseEvent readReply

/**
 * utility methods
 * **/

func (s *BaseEvent) String() string {
  var str string
  str += s.part1.EventType.String() + " "
  if len(s.part1.DestQueue)>0 { str += "q:'" + s.part1.DestQueue + "' " }
  if len(s.part1.Reference)>0 { str += "ref:'" + s.part1.Reference + "' " }
  if (len(s.part1.ReturnFd)>0)&&(s.part1.ReturnFd!="-1") { str += "rFd:" + s.part1.ReturnFd + " " }
  
  if s.sysParamsExtracted {
    if s.sysParams.Command!=CMD_NONE    { str += s.sysParams.Command.String() + " " }
    if len(s.sysParams.Url)>0           { str += "url:" + s.sysParams.Url + " " }
    if len(s.sysParams.ScriptName)>0    { str += "scriptName:'" + s.sysParams.ScriptName + "' " }
    if len(s.sysParams.Result)>0        { str += "result:'" + s.sysParams.Result + "' " }
    str += "bSuccess:" + fmt.Sprint(s.sysParams.BSuccess) + " "
    if len(s.sysParams.ErrorString)>0   { str += "errorString:" + s.sysParams.ErrorString + " " }
    if len(s.sysParams.FailureCause)>0  { str += "failureCause:" + s.sysParams.FailureCause + " " }
    if len(s.sysParams.SystemParam)>0   { str += "systemParam:" + s.sysParams.SystemParam + " " }
  } else if len(s.sysParamsJson)>0 {
    str += "sysParams:" + string(s.sysParamsJson) + " "
  } // else s.sysParamsExtracted
  if s.part2JsonExtracted {
    if len(s.part2.Trace)>0           { str += "traceB||" + s.part2.Trace + "||traceE " }
    if len(s.part2.TraceTimestamp)>0  { str += "traceTS:" + s.part2.TraceTimestamp + " " }
  } //
  return str
} // BaseEvent String

func (s *BaseEvent) GoString() string {
  var str string
  str += s.part1.EventType.String() + " "
  if len(s.part1.DestQueue)>0 { str += "q:'" + s.part1.DestQueue + "' " }
  if len(s.part1.Reference)>0 { str += "ref:'" + s.part1.Reference + "' " }
  if (len(s.part1.ReturnFd)>0)&&(s.part1.ReturnFd!="-1") { str += "rFd:'" + s.part1.ReturnFd + "' " }
  
  if s.sysParamsExtracted {
    if s.sysParams.BStandardResponse    { str += "bStandardResponse:" + fmt.Sprint(s.sysParams.BStandardResponse) + " " }
    if s.sysParams.Command!=CMD_NONE    { str += s.sysParams.Command.String() + " " }
    if len(s.sysParams.Url)>0           { str += "url:" + s.sysParams.Url + " " }
    if len(s.sysParams.ScriptName)>0    { str += "scriptName:'" + s.sysParams.ScriptName + "' " }
    if len(s.sysParams.Result)>0        { str += "result:'" + s.sysParams.Result + "' " }
    str += "bSuccess:" + fmt.Sprint(s.sysParams.BSuccess) + " "
    if len(s.sysParams.ErrorString)>0   { str += "errorString:" + s.sysParams.ErrorString + " " }
    if len(s.sysParams.FailureCause)>0  { str += "failureCause:" + s.sysParams.FailureCause + " " }
    if len(s.sysParams.SystemParam)>0   { str += "systemParam:" + s.sysParams.SystemParam + " " }
  } else if len(s.sysParamsJson)>0 {
    str += "sysParams:" + string(s.sysParamsJson) + " "
  } // else s.sysParamsExtracted
  
  if s.execParamsExtracted {
    s.serialiseExecParams()
    str += "execParams:" + string(s.execParamsJson) + " "
  } // if s.execParamsExtracted

  if s.part2JsonExtracted {
    if s.part2.ExpiryTime>0           { str += "expires:" + fmt.Sprint(s.part2.ExpiryTime) + " " }
    if s.part2.Lifetime>0             { str += "lifetime:" + fmt.Sprint(s.part2.Lifetime) + " " }
    if s.part2.Retries>0              { str += "retries:" + fmt.Sprint(s.part2.Retries) + " " }
    if len(s.part2.Trace)>0           { str += "traceB||" + s.part2.Trace + "||traceE " }
    if len(s.part2.TraceTimestamp)>0  { str += "traceTS:" + s.part2.TraceTimestamp + " " }
  } else if len(s.part2Json)>0 {
    str += "part2:" + string(s.part2Json) + " "
  } // else s.sysParamsExtracted

  str += "p:" + fmt.Sprintf( "%p", s )
  return str
} // BaseEvent GoString

func (s *BaseEvent) Error() string {
  // fmt.Print functions unfortunately invoke Error rather than String if available so 
  // if the error string is empty we are most likely invoked with the intent of displaying ourselves
  if len(s.errString)==0 {
    return s.String()
  } // if
  return s.errString
} // EEventType Error

/**
 * factory methods
 * **/

/**
 * unserialise from a socket
 * on stream sockets the greeting has to be read after connecting - user ReadGreeting
 * **/
func UnSerialiseFromSocket( conn net.Conn, parseAll bool ) (*BaseEvent,error) {
  // read the frame header #frameNewframe#v3.0:000441\n
  frameHeader := make( []byte, FRAME_HEADER_LEN, FRAME_HEADER_LEN )
  numRead,err := conn.Read( frameHeader )
  if numRead != FRAME_HEADER_LEN { return nil,errors.New( fmt.Sprintf( "UnSerialiseFromSocket failed read %d bytes frameHeader:%s - %s", numRead, string(frameHeader), err ) ) }

  // verify protocol version and extract payload length
  var packetLen int
  headerTemplate := fmt.Sprintf( "%s%s:%%d", FRAME_HEADER, PROTOCOL_VERSION_NUMBER )
  if numParsed,err:=fmt.Sscanf(string(frameHeader), headerTemplate, &packetLen); numParsed != 1 {
    newErr := errors.New( "UnSerialiseFromSocket error failed to parse header:" + err.Error() + " packet:"+string(frameHeader) )
    return nil,newErr
  } // if

  // read the payload
  totalBytesRead := 0
  err = nil
  packet := make( []byte, packetLen, packetLen )
  appLogger.Log.Printf( "debug UnSerialiseFromSocket trying to read %d bytes", packetLen )
  for (err==nil) && (totalBytesRead<packetLen) {
    partPacket := packet[totalBytesRead:]
    numRead,err = conn.Read( partPacket )
    totalBytesRead += numRead
    if Verbose {
      appLogger.Log.Print( "debug UnSerialiseFromSocket packetLen:", packetLen, " totalBytesRead:", totalBytesRead," numRead:", numRead, " partPacket:", string(partPacket), " packet:", string(packet), " err:", err )
    } else {
      appLogger.Log.Print( "debug UnSerialiseFromSocket packetLen:", packetLen, " totalBytesRead:", totalBytesRead," numRead:", numRead, " err:", err )
    } // else
  } // for
  if totalBytesRead < packetLen  {
    newErr := errors.New( fmt.Sprint( "UnSerialiseFromSocket error reading packet - expected ", packetLen, " bytes, got ", totalBytesRead, " - " + err.Error() ) )
    return nil,newErr
  } // if

  // parse the body
  event := new( BaseEvent )
  err = event.ParseBody( packet, parseAll )
  return event,err
} // UnSerialiseFromSocket

/**
 * un-serialise from a file
 * @param the filename
 * @return the newly constructed object or NULL on failure (including it not being a baseEvent serialised packet at all)
 * **/
func UnSerialiseFromFile( filename string, parseAll bool ) (*BaseEvent,error) {
  packet,err := ioutil.ReadFile( filename )
  if err != nil { return nil,errors.New( fmt.Sprint( "UnSerialiseFromFile failed on file: ", err ) ) }

  return UnSerialiseFromString( packet, parseAll )
} // UnSerialiseFromFile

/**
 * un-serialise from a string
 * @param the filename
 * @return the newly constructed object or NULL on failure (including it not being a baseEvent serialised packet at all)
 * **/
func UnSerialiseFromString( packet []byte, parseAll bool ) (*BaseEvent,error) {
  var packetLen uint
  headerTemplate := fmt.Sprintf( "%s%s:%%d", FRAME_HEADER, PROTOCOL_VERSION_NUMBER )
  if numParsed,err:=fmt.Sscanf(string(packet), headerTemplate, &packetLen); numParsed != 1 {
    newErr := errors.New( "UnSerialiseFromString error failed to parse header:" + err.Error() + " packet:"+string(packet) )
    return nil,newErr
  } // if

  body := packet[FRAME_HEADER_LEN:]
  event := new( BaseEvent )
  err := event.ParseBody( body, parseAll )
  return event,err
} // unSerialiseFromString

/**
 * factory function - please use instead of newing directly
 * **/
func NewBaseEvent( e EEventType ) (event *BaseEvent, err error) {
  event = new(BaseEvent)

  // setting these to true will force serialisation
  event.part2JsonExtracted = true
  event.sysParamsExtracted = true
  event.execParamsExtracted = true
  event.verbose = Verbose                 // pick up the global flag

  err = event.SetEventType(e)
  return
} // NewBaseEvent


/**
 * EEventType methods
 * **/
/**
 * validates an EEventType
 * **/
func (e EEventType) Validate() error {
  switch e {
    case EV_UNKNOWN,EV_BASE,EV_SCRIPT,EV_PERL,EV_BIN,EV_URL,EV_RESULT,EV_WORKER_DONE,EV_COMMAND,EV_REPLY,EV_ERROR:
      return nil
    default:
      return e      // error condition - Error() can be invoked on it
  } // switch
} // EEventType Validate
func (e EEventType) Error() string {
  return e.String()
} // EEventType Error

func (e EEventType) String() string {
  switch e {
    case EV_UNKNOWN:
      return "EV_UNKNOWN"
    case EV_BASE:
      return "EV_BASE"
    case EV_SCRIPT:
      return "EV_SCRIPT"
    case EV_PERL:
      return "EV_PERL"
    case EV_BIN:
      return "EV_BIN"
    case EV_URL:
      return "EV_URL"
    case EV_RESULT:
      return "EV_RESULT"
    case EV_WORKER_DONE:
      return "EV_WORKER_DONE"
    case EV_COMMAND:
      return "EV_COMMAND"
    case EV_REPLY:
      return "EV_REPLY"
    case EV_ERROR:
      return "EV_ERROR"
    default:
      return fmt.Sprintf( "unknown EEventType: %d", int(e) )
  } // switch
} // EEventType String

/**
 * ECommandType methods
 * **/
/**
 * validates an ECommandType
 * **/
func (c ECommandType) Validate() error {
  switch c {
    case CMD_NONE,CMD_STATS,CMD_RESET_STATS,CMD_REOPEN_LOG,CMD_REREAD_CONF,CMD_EXIT_WHEN_DONE,CMD_SEND_UDP_PACKET,CMD_TIMER_SIGNAL,CMD_CHILD_SIGNAL,CMD_APP,CMD_SHUTDOWN,CMD_NUCLEUS_CONF,CMD_DUMP_STATE,CMD_NETWORKIF_CONF,CMD_END_OF_QUEUE,CMD_MAIN_CONF,CMD_PERSISTENT_APP,CMD_EVENT,CMD_WORKER_CONF:
      return nil
    default:
      return c      // error condition - Error() can be invoked on it
  } // switch
} // ECommandType Validate
func (c ECommandType) Error() string {
  return c.String()
} // ECommandType Error

func (c ECommandType) String() string {
  switch c {
    case CMD_NONE:
      return "CMD_NONE"
    case CMD_STATS:
      return "CMD_STATS"
    case CMD_RESET_STATS:
      return "CMD_RESET_STATS"
    case CMD_REOPEN_LOG:
      return "CMD_REOPEN_LOG"
    case CMD_REREAD_CONF:
      return "CMD_REREAD_CONF"
    case CMD_EXIT_WHEN_DONE:
      return "CMD_EXIT_WHEN_DONE"
    case CMD_SEND_UDP_PACKET:
      return "CMD_SEND_UDP_PACKET"
    case CMD_TIMER_SIGNAL:
      return "CMD_TIMER_SIGNAL"
    case CMD_CHILD_SIGNAL:
      return "CMD_CHILD_SIGNAL"
    case CMD_APP:
      return "CMD_APP"
    case CMD_SHUTDOWN:
      return "CMD_SHUTDOWN"
    case CMD_NUCLEUS_CONF:
      return "CMD_NUCLEUS_CONF"
    case CMD_DUMP_STATE:
      return "CMD_DUMP_STATE"
    case CMD_NETWORKIF_CONF:
      return "CMD_NETWORKIF_CONF"
    case CMD_END_OF_QUEUE:
      return "CMD_END_OF_QUEUE"
    case CMD_MAIN_CONF:
      return "CMD_MAIN_CONF"
    case CMD_PERSISTENT_APP:
      return "CMD_PERSISTENT_APP"
    case CMD_EVENT:
      return "CMD_EVENT"
    case CMD_WORKER_CONF:
      return "CMD_WORKER_CONF"
    default:
      return fmt.Sprintf( "unknown ECommandType: %d", int(c) )
  } // switch
  return "unknown";
} // commandToString

/**
 * testing 
 * **/
func Test() {
  appLogger.Log.Print( "info  BaseEvent Test entry" )

//  testSubmitTcp()
//  testTcp()
  testUd()
//  testSerFile()
//  testSerStr()

  appLogger.Log.Print( "info  BaseEvent Test exit" )
} // test

func testTcp() {
  conn,err := net.Dial( "tcp", "localhost:txproc" )
  if err != nil {
    appLogger.Log.Print( "WARN testTcp Dial error:", err )
    return
  } // if err
  defer conn.Close()

  err = ReadGreeting( conn )
  if err != nil {
    appLogger.Log.Print( "WARN testTcp ReadGreeting error:", err )
    return
  } // if err

  event,err := NewBaseEvent( EV_URL )
  if err != nil { appLogger.Log.Print( "WARN testTcp new error:", err ); return }
  event.SetDestQueue( "default" )
  event.SetUrl( "http://localhost/t.php" )
  event.SetReturnFd("0")                            // indicate that we will be waiting for the result
  appLogger.Log.Print( "info  BaseEvent testTcp serialising event:", event )
  result,err := event.SerialiseToSocket( conn )
  if err != nil { appLogger.Log.Print( "WARN testTcp SerialiseToSocket error:", err ); return }
  if result == RESULT_RESPONSE {
    response,err := UnSerialiseFromSocket( conn, true )
    if err != nil { appLogger.Log.Print( "WARN testTcp response UnSerialiseFromSocket error:", err ); return }
    appLogger.Log.Print( "info  BaseEvent testTcp reponse:", response.String() )
  } else {
    appLogger.Log.Print( "info  BaseEvent testTcp submitted successfully" )
  } // else
} // testTcp

func testUd() {
//  rAddr,err := net.ResolveUnixAddr( "unixgram", "/var/log/txProc/txProc.sock" )
//  conn,err := net.DialUnix( "unixgram", nil, rAddr )
  conn,err := net.Dial( "unixgram", "/var/log/txProc/txProc.sock" )
  if err != nil {
    appLogger.Log.Print( "WARN testUd Dial error:", err )
    return
  } // if err
  defer conn.Close()

  event,err := NewBaseEvent( EV_URL )
  if err != nil { appLogger.Log.Print( "WARN testUd new error:", err ); return }
  event.SetDestQueue( "default" )
  event.SetUrl( "http://localhost/t.php" )
  appLogger.Log.Print( "info  BaseEvent testUd serialising event:", event )
  _,err = event.SerialiseToSocket( conn )
  if err != nil { appLogger.Log.Print( "WARN testUd SerialiseToSocket error:", err ); return }
} // testUd

func testSerFile() {
  event,err := UnSerialiseFromFile( "/home/gerhardus/temp/r000329_gqwAs1", true )
  appLogger.Log.Printf( "debug testSerFile event:%s", event.String() )
  err = event.SerialiseToFile( "/home/gerhardus/temp/r000329_gqwAs1.new" )
  if err != nil { appLogger.Log.Print( "WARN SerialiseToFile error:", err ) }
} // testSerFile

func testSerStr() {
  var event *BaseEvent
  var err error
  packet := "#frameNewframe#v3.0:000126\n04,1,000042,1,000000,1,000014,1,000031\n{\"eventType\":8,\"reference\":\"25841-17091\"}\n{\"command\":1}\n{\"time\":\"2013-08-29 05:20:29\"}\n"
  if event,err = UnSerialiseFromString( []byte(packet), true ); err!=nil {
    appLogger.Log.Print( "WARN testSerStr unserialise error:", err )
  } // if

  var strEvent string
  if strEvent,err = event.SerialiseToString(); err!=nil {
    appLogger.Log.Print( "WARN testSerStr serialise error:", err )
  } // if
  appLogger.Log.Printf( "debug testSerStr String() event:%s", event.String() )
  appLogger.Log.Printf( "debug testSerStr len:%d   packet:'%s'", len(packet), packet )
  appLogger.Log.Printf( "debug testSerStr len:%d strEvent:'%s'", len(strEvent), strEvent )
} // testSerStr

/**
 * **/
func init() {
} // init
