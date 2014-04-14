<?php
/**
 * txProc interface class
 * based on TxProc.pm
 *
 * $Id: txProc.inc.php 869 2010-10-27 13:10:45Z gerhardus $
 * Gerhardus Muller
 * Versioning: a.b.c a is a major release, b represents changes or new features, c represents bug fixes. 
 * @version 1.0.0		01/12/2009		Gerhardus Muller		mirrors v 1.0.0 of baseEvent.cpp - protocol v3.0
 * @version 1.0.1		22/04/2010		Gerhardus Muller		replace CMD_DUMMY_1 with CMD_END_OF_QUEUE
 *
 * **/


class txProc
{
  const FRAME_HEADER = '#frameNewframe#v';
  const PROTOCOL_VERSION_NUMBER = '3.0';
  const FRAME_HEADER_LEN = 27; # strlen(FRAME_HEADER)+strlen(PROTOCOL_VERSION_NUMBER)+8 - the 8 is ':%06u\n'
  const BLOCK_HEADER_LEN = 39;
  private $eventType = 0;
  private $part1 = array();
  private $part2 = array();
  private $sysParams = array();
  private $execParams = array();
  private $maxWaitTime = 0;
  private $beVerbose = 1;
  private $bStreamSocket = 0;

  /**
   *  constructor
   *  optional parameter sets the type
   *  **/
  function __construct()
  {
    if( func_num_args() == 1 ) $this->eventType(func_get_arg(0));
  } // construct

  # part1 properties eventType,reference,returnFd,destQueue
  public function eventType( )
  {
    if( func_num_args() == 1 )
    {
      $val = func_get_arg(0);
      if( $val == 'EV_UNKNOWN' )
      {
        $this->eventType = 0;
      } # if
      elseif( $val == 'EV_BASE' )
      {
        $this->eventType = 1;
      } # elseif
      elseif( $val == 'EV_SCRIPT' )
      {
        $this->eventType = 2;
      } # elseif
      elseif( $val == 'EV_PERL' )
      {
        $this->eventType = 3;
      } # elseif
      elseif( $val == 'EV_BIN' )
      {
        $this->eventType = 4;
      } # elseif
      elseif( $val == 'EV_URL' )
      {
        $this->eventType = 5;
      } # elseif
      elseif( $val == 'EV_RESULT' )
      {
        $this->eventType = 6;
      } # elseif
      elseif( $val == 'EV_WORKER_DONE' )
      {
        $this->eventType = 7;
      } # elseif
      elseif( $val == 'EV_COMMAND' )
      {
        $this->eventType = 8;
      } # elseif
      elseif( $val == 'EV_REPLY' )
      {
        $this->eventType = 9;
      } # elseif
      elseif( $val == 'EV_ERROR' )
      {
        $this->eventType = 10;
      } # elseif
      else
      {
        trace( "TxProc::command unknown command '$val'", TRACE_WARN );
      } # else
    } # if
    $textVal = array('EV_UNKNOWN','EV_BASE','EV_SCRIPT','EV_PERL','EV_BIN',
      'EV_URL','EV_RESULT','EV_WORKER_DONE','EV_COMMAND', 'EV_REPLY','EV_ERROR');
    return $textVal[$this->eventType];
  } # eventType
  public function reference( )
  {
    if( func_num_args() == 1 )
      $this->part1['reference'] = func_get_arg(0);
    return $this->part1['reference'];
  } # sub reference
  public function returnFd( )
  {
    if( func_num_args() == 1 )
    {
      $val = func_get_arg(0);
      $this->part1['returnFd'] = "$val";  // has to be a string
    } // if
    return $this->part1['returnFd'];
  } # sub returnFd
  public function destQueue( )
  {
    if( func_num_args() == 1 )
      $this->part1['destQueue'] = func_get_arg(0);
    return $this->part1['destQueue'];
  } # sub destQueue

  # part2 properties - trace,traceTimestamp,expiryTime,lifetime,retries
  public function trace( )
  {
    if( func_num_args() == 1 )
      $this->part2['trace'] = func_get_arg(0);
    return $this->part2['trace'];
  } # public function trace
  public function traceTimestamp( )
  {
    if( func_num_args() == 1 )
      $this->part2['traceTimestamp'] = func_get_arg(0);
    return $this->part2['traceTimestamp'];
  } # public function traceTimestamp
  public function expiryTime( )
  {
    if( func_num_args() == 1 )
      $this->part2['expiryTime'] = func_get_arg(0);
    return $this->part2['expiryTime'];
  } # public function expiryTime
  public function lifetime( )
  {
    if( func_num_args() == 1 )
      $this->part2['lifetime'] = func_get_arg(0);
    return $this->part2['lifetime'];
  } # public function lifetime
  public function retries( )
  {
    if( func_num_args() == 1 )
      $this->part2['retries'] = func_get_arg(0);
    return $this->part2['retries'];
  } # public function retries

  # sysParams - bStandardResponse,command,url,scriptName,result,bSuccess,bExpectReply,
  # errorString,failureCause,systemParam,elapsedTime,bGeneratedRecoveryEvent
  # 
  public function bStandardResponse( )
  {
    if( func_num_args() == 1 )
      $this->sysParams['bStandardResponse'] = func_get_arg(0);
    return $this->sysParams['bStandardResponse'];
  } # sub bStandardResponse
  public function command( $val )
  {
    if( func_num_args() == 1 )
    {
      if( $val == 'CMD_NONE' )
      {
        $this->sysParams['command'] = 0;
      } # if
      elseif( $val == 'CMD_STATS' )
      {
        $this->sysParams['command'] = 1;
      } # elsif
      elseif( $val == 'CMD_RESET_STATS' )
      {
        $this->sysParams['command'] = 2;
      } # elsif
      elseif( $val == 'CMD_REOPEN_LOG' )
      {
        $this->sysParams['command'] = 3;
      } # elsif
      elseif( $val == 'CMD_REREAD_CONF' )
      {
        $this->sysParams['command'] = 4;
      } # elsif
      elseif( $val == 'CMD_EXIT_WHEN_DONE' )
      {
        $this->sysParams['command'] = 5;
      } # elsif
      elseif( $val == 'CMD_SEND_UDP_PACKET' )
      {
        $this->sysParams['command'] = 6;
      } # elsif
      elseif( $val == 'CMD_TIMER_SIGNAL' )
      {
        $this->sysParams['command'] = 7;
      } # elsif
      elseif( $val == 'CMD_CHILD_SIGNAL' )
      {
        $this->sysParams['command'] = 8;
      } # elsif
      elseif( $val == 'CMD_APP' )
      {
        $this->sysParams['command'] = 9;
      } # elsif
      elseif( $val == 'CMD_SHUTDOWN' )
      {
        $this->sysParams['command'] = 10;
      } # elsif
      elseif( $val == 'CMD_NUCLEUS_CONF' )
      {
        $this->sysParams['command'] = 11;
      } # elsif
      elseif( $val == 'CMD_DUMP_STATE' )
      {
        $this->sysParams['command'] = 12;
      } # elsif
      elseif( $val == 'CMD_NETWORKIF_CONF' )
      {
        $this->sysParams['command'] = 13;
      } # elsif
      elseif( $val == 'CMD_END_OF_QUEUE' )
      {
        $this->sysParams['command'] = 14;
      } # elsif
      elseif( $val == 'CMD_MAIN_CONF' )
      {
        $this->sysParams['command'] = 15;
      } # elsif
      elseif( $val == 'CMD_PERSISTENT_APP' )
      {
        $this->sysParams['command'] = 16;
      } # elsif
      elseif( $val == 'CMD_EVENT' )
      {
        $this->sysParams['command'] = 17;
      } # elsif
      elseif( $val == 'CMD_WORKER_CONF' )
      {
        $this->sysParams['command'] = 18;
      } # elsif
      else
      {
        trace( "dispatchCommandEvent::command unknown command '$val'", TRACE_WARN );
      } # else
    } // if
    $textVal = array('CMD_NONE','CMD_STATS','CMD_RESET_STATS','CMD_REOPEN_LOG','CMD_REREAD_CONF',
      'CMD_EXIT_WHEN_DONE','CMD_SEND_UDP_PACKET','CMD_TIMER_SIGNAL','CMD_CHILD_SIGNAL','CMD_APP',
      'CMD_SHUTDOWN','CMD_NUCLEUS_CONF','CMD_DUMP_STATE','CMD_NETWORKIF_CONF','CMD_END_OF_QUEUE',
      'CMD_MAIN_CONF','CMD_PERSISTENT_APP','CMD_EVENT','CMD_WORKER_CONF');
    return $textVal[$this->sysParams['command']];
  } // command
  public function url( )
  {
    if( func_num_args() == 1 )
      $this->sysParams['url'] = func_get_arg(0);
    return $this->sysParams['url'];
  } # public function url
  public function scriptName( )
  {
    if( func_num_args() == 1 )
      $this->sysParams['scriptName'] = func_get_arg(0);
    return $this->sysParams['scriptName'];
  } # public function scriptName
  public function result( )
  {
    if( func_num_args() == 1 )
      $this->sysParams['result'] = func_get_arg(0);
    return $this->sysParams['result'];
  } # public function result
  public function bSuccess( )
  {
    if( func_num_args() == 1 )
      $this->sysParams['bSuccess'] = func_get_arg(0);
    return $this->sysParams['bSuccess'];
  } # public function bSuccess
  public function bExpectReply( )
  {
    if( func_num_args() == 1 )
      $this->sysParams['bExpectReply'] = func_get_arg(0);
    return $this->sysParams['bExpectReply'];
  } # public function bExpectReply
  public function errorString( )
  {
    if( func_num_args() == 1 )
      $this->sysParams['errorString'] = func_get_arg(0);
    return $this->sysParams['errorString'];
  } # public function errorString
  public function failureCause( )
  {
    if( func_num_args() == 1 )
      $this->sysParams['failureCause'] = func_get_arg(0);
    return $this->sysParams['failureCause'];
  } # public function failureCause
  public function systemParam( )
  {
    if( func_num_args() == 1 )
      $this->sysParams['systemParam'] = func_get_arg(0);
    return $this->sysParams['systemParam'];
  } # public function systemParam
  public function elapsedTime( )
  {
    if( func_num_args() == 1 )
      $this->sysParams['elapsedTime'] = func_get_arg(0);
    return $this->sysParams['elapsedTime'];
  } # public function elapsedTime
  public function bGeneratedRecoveryEvent( )
  {
    if( func_num_args() == 1 )
      $this->sysParams['bGeneratedRecoveryEvent'] = func_get_arg(0);
    return $this->sysParams['bGeneratedRecoveryEvent'];
  } # public function bGeneratedRecoveryEvent

  # execParams
  public function addParam( $key, $val )
  {
    $this->execParams[$key] = $val;
  } # addParam
  # @param the key of the entry to retrieve
  public function getParam( $key )
  {
    if( isset($this->execParams[$key] ) )
      return $this->execParams[$key];
    else
      return NULL;
  } # getParam
  # @param the key of the entry to delete
  public function deleteParam( $key )
  {
    unset( $this->execParams[$key] );
  } # deleteParam
  public function execParams( ) 
  {
    if( func_num_args() == 1 )
    {
      $val = func_get_arg(0);
      if( is_array( $val ) )
        $this->execParams = $val;
      else
        trace( "txProc::execParams supplied value ref ".gettype($val)." value '$val'  is not an array", TRACE_WARN );
    }
    return $this->execParams;
  } # sub execParams

  # execParams
  # positional parameters
  public function addScriptParam( $val )
  {
    $this->execParams[] = $val;
  } # addScriptParam
  # @param the value to add to the beginning of the list of parameters
  public function prependScriptParam( $val )
  {
    array_unshift( $this->execParams, $val );
  } # prependScriptParam
  public function getScriptParam( $index )
  {
    if( isset($this->execParams[$index] ) )
      return $this->execParams[$index];
    else
      return NULL;
  } # getScriptParam
  # @param the index of the entry to set
  public function setScriptParam( $index, $val )
  {
    $this->execParams[$index] = $val;
  } # setScriptParam
  public function deleteScriptParam( $index )
  {
    unset( $this->execParams[$index] );
  } # deleteScriptParam
  public function shiftScriptParam( $index )
  {
    return array_shift( $this->execParams );
  } # shiftScriptParam

  // text representation
  public function toString()
  {
    $str = $this->propertiesToString();
    return $str;
  } // toString
  public function propertiesToString()
  {
    $str = '';
    foreach( $this as $key => $value )
    {
      if( is_scalar($value) )
      {
        $str .= "$key=>'$value',";
      } // else
      elseif( is_array($value) )
      {
        $str .= "HASH-$key=>".$this->hashToString($value).",";
      } // elseif
      else
      {
        $str .= "$key=>'cannot render',";
      } // else
    } // foreach

    return $str;
  } // propertiesToString
  public function hashToString( $param )
  {
    $text = "##[##";
    foreach( $this as $key => $value )
    {
      $text .= "$key=>'$value',";
    } // foreach
    $text .= "##]##";
    return $text;
  } // hashToString


  ##
  # serialisation / deserialisation support
  # ##
  public function serialisePart1( )
  {
    $this->part1['eventType'] = $this->eventType;
    $jsonStr = json_encode( $this->part1 );
    return $jsonStr;
  } # serialisePart1
  public function serialisePart2( )
  {
    $jsonStr = '';
    if( count($this->part2) > 0 )
      $jsonStr = json_encode( $this->part2 );
    return $jsonStr;
  } # serialisePart2
  public function serialiseSysParams( )
  {
    $jsonStr = '';
    if( count($this->sysParams) > 0 )
      $jsonStr = json_encode( $this->sysParams );
    return $jsonStr;
  } # serialiseSysParams
  public function serialiseExecParams( )
  {
    $jsonStr = '';
    if( count($this->execParams) > 0 )
      $jsonStr = json_encode( $this->execParams );
    return $jsonStr;
  } # serialiseExecParams

  public function serialiseToString()
  {
    $objStr = '';
    $jsonPart1 = $this->serialisePart1();
    $jsonPart2 = $this->serialisePart2();
    $jsonSysParams = $this->serialiseSysParams();
    $jsonExecParams = $this->serialiseExecParams();
    $payloadLen = self::BLOCK_HEADER_LEN+strlen($jsonPart1)+strlen($jsonPart2)+strlen($jsonSysParams)+strlen($jsonExecParams);

    $payloadHeader = sprintf( "%s%s:%06u\n%02u,1,%06u,1,%06u,1,%06u,1,%06u\n",self::FRAME_HEADER,self::PROTOCOL_VERSION_NUMBER,
      $payloadLen,4,strlen($jsonPart1),strlen($jsonPart2),strlen($jsonSysParams),strlen($jsonExecParams) );
    $strSerialised = $payloadHeader.$jsonPart1.$jsonPart2.$jsonSysParams.$jsonExecParams;

    return $strSerialised;
  } # serialiseToString

  /**
   * read the socket greeting for socket streams
   * **/
  public function readGreeting( $socket )
  {
    # read '024:'
    $lenStr = socket_read( $socket, 4 );
    if( $lenStr === FALSE ) return array(0, "txProc::readGreeting read 1 failed - ".socket_strerror(socket_last_error()) );
    $lenStr = rtrim( $lenStr, ':' );  # remove the ':'

    # read the rest of the greeting 'program@host pver x.x'
    $greeting = socket_read( $socket, $lenStr );
    if( $greeting === FALSE ) return array(0, "txProc::readGreeting read 2 failed - ".socket_strerror(socket_last_error()) );

    $pver = '';
    if( preg_match('/pver ([\d.]+)/', $greeting, $matches) ) $pver = $matches[1];
    if( $pver != self::PROTOCOL_VERSION_NUMBER ) return array(0,"txProc::readGreeting failed on protocol version - expected:'".self::PROTOCOL_VERSION_NUMBER."' got:'$pver' greeting:'$greeting'");
    return array( 1, NULL );
  } // readGreeting

  # read stream based reply
  # @return (0,failReason) or (1,'') for no response or (2,'') to expect a response
  public function readReply( $socket )
  {
    list($reply,$err) = $this->unSerialise($socket);
    if( !$reply ) return array(0,$err);
    return array(1+$reply->bExpectReply(),'');
  } # readReply

  # @return true if data available or false if not
  public function waitForData( $socket )
  {
    $readArr = array( $socket );
    $writeArr = NULL;
    $exceptArr = NULL;
    $socketsReady = socket_select( $readArr, $writeArr, $exceptArr, $maxWaitTime );
    if( $socketsReady === false )
      return array( 0, "eventArchive::readReplyPacket select failed - ".socket_strerror(socket_last_error()) );
    elseif( $socketsReady == 0 )
      return array( 1, "eventArchive::readReplyPacket select timed out");

    return array( 2, "eventArchive::readReplyPacket select timed out");
  } # waitForData

  # parses the body and populates the object
  # @return (1,'') or (0,errStr)
  public function parseBody( $body )
  {
    $numSections = 0;
    $part1Size = 0;
    $part2Size = 0;
    $sysSize = 0;
    $execSize = 0;
    if( preg_match('/^(\d+),1,(\d+),1,(\d+),1,(\d+),1,(\d+)/', $body, $matches) )
    {
      $numSections = $matches[1];
      $part1Size = $matches[2];
      $part2Size = $matches[3];
      $sysSize = $matches[4];
      $execSize = $matches[5];

      if( $numSections != 4 ) return array(0, "txProc::parseBody: expected 4 sections found:$numSections" );
      if( $part1Size == 0 )return array(0, "txProc::parseBody: part1 cannot be empty" );
    } # if
    else
    { 
      return array(0,"txProc::parseBody: failed to parse body header:'$body'");
    } # else

    $startOffset = self::BLOCK_HEADER_LEN;

    $jsonPart1 = substr( $body, $startOffset, $part1Size );
    $startOffset += $part1Size;
    $this->part1 = json_decode( $jsonPart1, true );
//    print( "jsonPart1:$jsonPart1: decoded:\n" );
//    print_r( $this->part1 );
//    print( " done-jsonPart1\n" );
    $this->eventType = $this->part1['eventType'];

    if( $part2Size > 0 )
    {
      $jsonPart2 = substr( $body, $startOffset, $part2Size );
      $startOffset += $part2Size;
      $this->part2 = json_decode( $jsonPart2, true );
    } # if

    if( $sysSize > 0 )
    {
      $jsonSysParams = substr( $body, $startOffset, $sysSize );
      $startOffset += $sysSize;
      $this->sysParams = json_decode( $jsonSysParams, true );
    } # if

    if( $execSize > 0 )
    {
      $jsonExecParams = substr( $body, $startOffset, $execSize );
      $startOffset += $execSize;
      $this->execParams = json_decode( $jsonExecParams, true );
    } # if

    if( $this->beVerbose ) trace( "txProc::parseBody: part1Size:$part1Size part2Size:$part2Size sysSize:$sysSize execSize:$execSize\nbody:'$body'" );
    return array(1,NULL);
  } # parseBody

  # static method
  # precede with a call to waitForData if non-blocking and required to wait
  # a tacid assumption though not a particularly good one is that the socket
  # buffer will contain at least enough data for the body as well ie the packet
  # is not fragmented. this assumption is no good for larger packets (>mtu) 
  # over the internet
  public static function unSerialise( $socket )
  {
    # read the header
    $header = socket_read( $socket, self::FRAME_HEADER_LEN );  # read the frame header, protocol version and payload length including the \n at the end
    if( $header === FALSE ) return array(0, "txProc::unSerialise no data" );

    # verify protocol version and extract payload length
    $packetLen = 0;
    $headerTemplate = sprintf( "^%s%s:(\\d+)", self::FRAME_HEADER,self::PROTOCOL_VERSION_NUMBER );
    if( preg_match("/$headerTemplate/", $header, $matches) )
      $packetLen = $matches[1];
    else
      return array(0, "txProc::unSerialise failed to parse the header:'$header'" );

    # read the body
    $body = socket_read( $socket, $packetLen );
    if( ($body === FALSE) || (strlen($body)!=$packetLen) )
      return array(0, "txProc::unSerialise read body len:$packetLen failed; read ".strlen($body)." bytes - ".socket_strerror(socket_last_error()) ) ;

    # deserialise
    $newObject = new txProc();
    list($retVal,$errStr) = $newObject->parseBody( $body );
    if( $retVal ) return array($newObject,NULL);
    return array($retVal,$errStr);
  } # unSerialise

  /**
   * utility function to create a socket with
   * @param $unixdomainPath - pass as NULL to not use but takes preference if defined
   * @param $serverName
   * @param $serverService
   * @return ([FALSE fail, $socket],error string)
   * **/
  public function createSocket( $unixdomainPath,$serverName,$serverService )
  {
    $socket = FALSE;
    if( isset($unixdomainPath) )
    {
      $this->bStreamSocket = 0;
      $socket = socket_create( AF_INET, SOCK_DGRAM, SOL_UDP );
      if( $socket === FALSE ) return array( false, "createSocket: could not create SOCK_DGRAM: ".socket_strerror(socket_last_error()) );
      $result = socket_connect( $socket, $unixdomainPath );
      if( $result === FALSE ) return array( false, "createSocket: failed to connect - socket:$unixdomainPath ".socket_strerror(socket_last_error()) );
    } // if $unixdomainPath
    elseif( isset($serverName) && ($serverName == '-') )
    {
      // this option is not currently supported - most likely not required either
      //        $this->bStreamSocket = 0;
      //        $bPipe = 1;
      //        $socket = new IO::Handle;
      //        $socket->fdopen( fileno(STDOUT), "w" );
      return array(false, "createSocket: serverName == '-' not supported");
    } # elsif
    elseif( isset($serverName) && isset($serverService) )
    {
      $this->bStreamSocket = 1;
      $port = getservbyname( $serverService, "tcp" ); // error checking is not that easy :(
      $ip = gethostbyname( $serverName );
      if( $port === FALSE ) return array( false, "createSocket: failed - unable to lookup service:'$serverService'" );

      $socket = socket_create( AF_INET, SOCK_STREAM, SOL_TCP );
      if( $socket === FALSE ) return array( false, "createSocket: failed - could not create socket: ".socket_strerror(socket_last_error()) );
      $result = socket_connect( $socket, $ip, $port );
      if( $result === FALSE ) return array( false, "createSocket: failed to connect - ip:$ip, port:$port server:$serverName service:$serverService: ".socket_strerror(socket_last_error()) );
    } # if
    else
    {
      return array(0, "createSocket: no socket definitions available");
    } # else

    return array($socket, NULL);
  } // createSocket

  /**
   * @param $socket - if a socket object is passed in it is assumed it is a TCP/stream socket object
   * @param $unixdomainPath - pass as NULL to not use but takes preference if defined
   * @param $serverName
   * @param $serverService
   * @return ((0 fail, 1 success, 2 expect response),error string)
   * **/
  public function serialise( $socket,$unixdomainPath,$serverName,$serverService )
  {
    # if we were not given a socket object then create one
    $retVal = 0;
    $errorString = '';
    $this->bStreamSocket = 0;
    $bPipe = 0;
    $bLocalSocketCreated = 1;

    if( isset($socket) )
    {
      $this->bStreamSocket = 1;
      $bLocalSocketCreated = 0;
    } // if
    else
    {
      list($socket,$err) = $this->createSocket( $unixdomainPath,$serverName,$serverService );
      if( $socket === false ) return array($socket,$err);
    } // else isset socket

    # serialise and get the actual payload
    $payload = $this->serialiseToString();
    if($this->beVerbose) trace( "txProc::serialise: '$payload'", TRACE_DEBUG );

    # if a stream socket first read the greeting
    if( $this->bStreamSocket )
    {
      list($retVal,$errorString) = $this->readGreeting( $socket );
      if( !$retVal )
      {
        if( $bLocalSocketCreated ) socket_close($socket);
        return array($retVal,$errorString);
      } #if
    } # if $bStreamSocket

    # send to the socket
    if( $bPipe )
    {
      trace( "txProc::serialise: pipes are not supported", TRACE_ERROR );
    } // if
    else
    {
      $retVal = socket_write( $socket, $payload );
      if( $retVal > 0 ) $retVal = 1;
    } // else
    if( !$retVal )
    {
      $errorString = "txProc::serialise unix domain send failed - path:'$unixdomainPath' - $@";
      if( $bLocalSocketCreated ) socket_close($socket);
      return array($retVal,$errorString);
    } // if

    # read success or failure on submission and if we should wait for a response object
    if( $this->bStreamSocket )
    {
      list($retVal,$errorString) = $this->readReply( $socket );
    } // if $bStreamSocket

    # cleanup if required
    if( $bLocalSocketCreated )
    {
      socket_close( $socket );
      $socket = 0;
    } // if

    return array($retVal,$errorString);
  } // serialise
} // class txProc
?>
