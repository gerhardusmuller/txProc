# txProc interface class
#
# Versioning: a.b.c a is a major release, b represents changes or new features, c represents bug fixes. 
# @version 1.0.0		05/11/2014		Gerhardus Muller		script created
#
# Copyright Gerhardus Muller
#
# zypper install python3-pip
# pip install enum34
#
# import sys
# sys.path
#
# pretty printing json
# echo '{1.2:3.4}' | python -m json.tool
#
import json
import re
import socket
import os
import sys
import io
import Log
import logging
from enum import Enum
from numbers import Number
from TxProcException import TxProcException

FRAME_HEADER = '#frameNewframe#v'
PROTOCOL_VERSION_NUMBER = '3.0'
FRAME_HEADER_LEN = 27               # strlen(FRAME_HEADER)+strlen(PROTOCOL_VERSION_NUMBER)+8 - the 8 is ':%06u\n'
BLOCK_HEADER_LEN = 39
READ_BUF_SIZE = 32768
MAX_HEADER_BLOCK_LEN = 999999       # corresponds to %06d
log = Log.GetLogger( __name__ )


class eEventType( Enum ):
  EV_UNKNOWN = 0
  EV_BASE = 1
  EV_SCRIPT = 2
  EV_PERL = 3
  EV_BIN = 4
  EV_URL = 5
  EV_RESULT = 6
  EV_WORKER_DONE = 7
  EV_COMMAND = 8
  EV_REPLY = 9
  EV_ERROR = 10

class eCommandType( Enum ):
  CMD_NONE = 0
  CMD_STATS = 1
  CMD_RESET_STATS = 2
  CMD_REOPEN_LOG = 3
  CMD_REREAD_CONF = 4
  CMD_EXIT_WHEN_DONE = 5
  CMD_SEND_UDP_PACKET = 6
  CMD_TIMER_SIGNAL = 7
  CMD_CHILD_SIGNAL = 8
  CMD_APP = 9
  CMD_SHUTDOWN = 10
  CMD_NUCLEUS_CONF = 11
  CMD_DUMP_STATE = 12
  CMD_NETWORKIF_CONF = 13
  CMD_END_OF_QUEUE = 14
  CMD_MAIN_CONF = 15
  CMD_PERSISTENT_APP = 16
  CMD_EVENT = 17
  CMD_WORKER_CONF = 18

class TxProc:
  # constructor
  def __init__( self, eType=eEventType.EV_UNKNOWN ):
    self.eventType = eType
    self.part1 = {}
    self.part2 = {}
    self.sysParams = {}
    self.execParams = {}
    self.maxWaitTime = 0
    self.selectObject = 0
    self.destServer = None
    self.beVerbose = False
    self.prettyPrint = False
    self.jsonOptions = {'separators':(',',':')}

  def PrettyPrint( self ):
    self.prettyPrint = True
    self.jsonOptions['indent'] = 2
    self.jsonOptions['separators'] = (', ',': ')

  def MaxWaitTime( self, val=None ):
    if val != None: self.maxWaitTime = val
    return self.maxWaitTime

  def BeVerbose( self, val=None ):
    if val != None: self.beVerbose = val
    return self.beVerbose

  def DestServer( self, val=None ):
    if val != None: self.destServer = val
    return self.destServer

  # part1 properties eventType,reference,returnFd,destQueue
  # val has to be a eEventType. value
  def EventType( self, val=None ):
    if val != None:
      if not isinstance(val, eEventType): raise TxProcException( "TxProc::EventType value passed has to be of type eEventType" )
      self.eventType = val
    return self.eventType

  def Reference( self, val=None ):
    if val != None: self.part1['reference'] = val
    return self.part1['reference'] if 'reference' in self.part1 else None

  def ReturnFd( self, val=None ):
    if val != None: self.part1['returnFd'] = val
    return self.part1['returnFd'] if 'returnFd' in self.part1 else None

  def DestQueue( self, val=None ):
    if val != None: self.part1['destQueue'] = val
    return self.part1['destQueue'] if 'destQueue' in self.part1 else None

  # part2 properties - trace,traceTimestamp,expiryTime,lifetime,retries,workerPid(wpid)
  def Trace( self, val=None ):
    if val != None: self.part2['trace'] = val
    return self.part2['trace'] if 'trace' in self.part2 else None

  def TraceTimestamp( self, val=None ):
    if val != None: self.part2['traceTimestamp'] = val
    return self.part2['traceTimestamp'] if 'traceTimestamp' in self.part2 else None

  def ExpiryTime( self, val=None ):
    if val != None: self.part2['expiryTime'] = val
    return self.part2['expiryTime'] if 'expiryTime' in self.part2 else None

  def Lifetime( self, val=None ):
    if val != None: self.part2['lifetime'] = val
    return self.part2['lifetime'] if 'lifetime' in self.part2 else None

  def Retries( self, val=None ):
    if val != None: self.part2['retries'] = val
    return self.part2['retries'] if 'retries' in self.part2 else None

  def Wpid( self, val=None ):
    if val != None: self.part2['wpid'] = val
    return self.part2['wpid'] if 'wpid' in self.part2 else None

  # sysParams - bStandardResponse,command,url,scriptName,result,bSuccess,bExpectReply,
  # errorString,failureCause,systemParam,elapsedTime,bGeneratedRecoveryEvent
  # val has to be 0 or 1
  def BStandardResponse( self, val=None ):
    if val != None: self.sysParams['bStandardResponse'] = val
    return self.sysParams['bStandardResponse'] if 'bStandardResponse' in self.sysParams else None

  # val has to be of type eCommandType
  def Command( self, val=None ):
    if val != None:
      if not isinstance(val, eCommandType): raise TxProcException( "TxProc::Command value passed has to be of type eCommandType" )
      self.sysParams['command'] = val.value
    return eCommandType(self.sysParams['command']) if 'command' in self.sysParams else None

  def Url( self, val=None ):
    if val != None: self.sysParams['url'] = val
    return self.sysParams['url'] if 'url' in self.sysParams else None

  def ScriptName( self, val=None ):
    if val != None: self.sysParams['scriptName'] = val
    return self.sysParams['scriptName'] if 'scriptName' in self.sysParams else None

  def Result( self, val=None ):
    if val != None: self.sysParams['result'] = val
    return self.sysParams['result'] if 'result' in self.sysParams else None

  # val has to be 0 or 1
  def BSuccess( self, val=None ):
    if val != None: self.sysParams['bSuccess'] = val
    return self.sysParams['bSuccess'] if 'bSuccess' in self.sysParams else None

  # val has to be 0 or 1
  def BExpectReply( self, val=None ):
    if val != None: self.sysParams['bExpectReply'] = val
    return self.sysParams['bExpectReply'] if 'bExpectReply' in self.sysParams else None

  def ErrorString( self, val=None ):
    if val != None: self.sysParams['errorString'] = val
    return self.sysParams['errorString'] if 'errorString' in self.sysParams else None

  def FailureCause( self, val=None ):
    if val != None: self.sysParams['failureCause'] = val
    return self.sysParams['failureCause'] if 'failureCause' in self.sysParams else None

  def SystemParam( self, val=None ):
    if val != None: self.sysParams['systemParam'] = val
    return self.sysParams['systemParam'] if 'systemParam' in self.sysParams else None

  def ElapsedTime( self, val=None ):
    if val != None: self.sysParams['elapsedTime'] = val
    return self.sysParams['elapsedTime'] if 'elapsedTime' in self.sysParams else None

  # val has to be 0 or 1
  def BGeneratedRecoveryEvent( self, val=None ):
    if val != None: self.sysParams['bGeneratedRecoveryEvent'] = val
    return self.sysParams['bGeneratedRecoveryEvent'] if 'bGeneratedRecoveryEvent' in self.sysParams else None

  # execParams
  # named parameters
  def AddParam( self, key, val ):
    self.execParams[key] = val

  def GetParam( self, key ):
    return self.execParams[key] if key in self.execParams else None

  def DeleteParam( self, key ):
    if key in self.execParams: del self.execParams[key]

  def ExecParams( self ):
    return self.execParams

  # execParams
  # positional parameters
  def AddScriptParam( self, val ):
    if isinstance(self.sysParams, dict): self.execParams = []
    self.execParams.append( val )

  def PrependScriptParam( self, val ):
    if isinstance(self.sysParams, dict): self.execParams = []
    self.execParams.insert( 0, val )
    
  def GetScriptParam( self, index ):
    if index>=0 and index<self.execParams.count:
      return self.execParams[index]
    else:
      return None

  def SetScriptParam( self, index, val ):
    if index>=0 and index<self.execParams.count:
      self.execParams[index] = val

  def DeleteScriptParam( self, index ):
    if index>=0 and index<self.execParams.count:
      del self.execParams[index]

  def ShiftScriptParam( self ):
    if self.execParams.count > 0:
      return self.execParams.pop(0)
    else:
      return None

  # text representation
  # invoke with str(object)
  def __repr__( self ):
    separator = '\n' if self.prettyPrint else ' '
    strVal = str(self.eventType) + separator

    if bool(self.part1):
      strVal = strVal + '==part1:' + json.dumps( self.part1, **self.jsonOptions ) + separator
    if bool(self.sysParams):
      strVal = strVal + '==sysParams:' + json.dumps( self.sysParams, **self.jsonOptions ) + separator
    if bool(self.execParams):
      strVal = strVal + '==execParams:' + json.dumps( self.execParams, **self.jsonOptions ) + separator
    if bool(self.part2):
      strVal = strVal + '==part2:' + json.dumps( self.part2, **self.jsonOptions ) + separator
    return strVal
    ##__repr__
      
  # serialisation / deserialisation support
  def SerialisePart1( self ):
    self.part1['eventType'] = self.eventType.value
    return json.dumps( self.part1, separators=(',', ':') )
    ##SerialisePart1

  def SerialisePart2( self ):
    # json c++ code has a hernia if these are not integers
    if 'expiryTime' in self.part2 and not isinstance(self.part2['expiryTime'], Number):
      self.part2['expiryTime'] = int( self.part2['expiryTime'] )
    if 'lifetime' in self.part2 and not isinstance(self.part2['lifetime'], Number):
      self.part2['lifetime'] = int( self.part2['lifetime'] )
    if 'retries' in self.part2 and not isinstance(self.part2['retries'], Number):
      self.part2['retries'] = int( self.part2['retries'] )
    if 'wpid' in self.part2 and not isinstance(self.part2['wpid'], Number):
      self.part2['wpid'] = int( self.part2['wpid'] )
    return json.dumps( self.part2, separators=(',', ':') )
    ##SerialisePart2

  def SerialiseSysParams( self ):
    # json c++ code has a hernia if these are not integers
    if 'command' in self.sysParams and not isinstance(self.sysParams['command'], Number):
      self.sysParams['command'] = int( self.sysParams['command'] )
    if 'bStandardResponse' in self.sysParams and not isinstance(self.sysParams['bStandardResponse'], Number):
      self.sysParams['bStandardResponse'] = int( self.sysParams['bStandardResponse'] )
    if 'bSuccess' in self.sysParams and not isinstance(self.sysParams['bSuccess'], Number):
      self.sysParams['bSuccess'] = int( self.sysParams['bSuccess'] )
    if 'bExpectReply' in self.sysParams and not isinstance(self.sysParams['bExpectReply'], Number):
      self.sysParams['bExpectReply'] = int( self.sysParams['bExpectReply'] )
    if 'elapsedTime' in self.sysParams and not isinstance(self.sysParams['elapsedTime'], Number):
      self.sysParams['elapsedTime'] = int( self.sysParams['elapsedTime'] )
    if 'bGeneratedRecoveryEvent' in self.sysParams and not isinstance(self.sysParams['bGeneratedRecoveryEvent'], Number):
      self.sysParams['bGeneratedRecoveryEvent'] = int( self.sysParams['bGeneratedRecoveryEvent'] )
    return json.dumps( self.sysParams, separators=(',', ':') )
    ##SerialiseSysParams

  def SerialiseExecParams( self ):
    return json.dumps( self.execParams, separators=(',', ':') )
    ##SerialiseExecParams

  def SerialiseToString( self ):
    jsonPart1 = self.SerialisePart1()
    jsonPart2 = self.SerialisePart2()
    jsonSysParams = self.SerialiseSysParams()
    jsonExecParams = self.SerialiseExecParams()
    payloadLen = BLOCK_HEADER_LEN+len(jsonPart1)+len(jsonPart2)+len(jsonSysParams)+len(jsonExecParams)
    if payloadLen > MAX_HEADER_BLOCK_LEN: raise TxProcException( 'TxProc::SerialiseToString payloadLen:'+str(payloadLen)+'exceeds max' )

    # sprintf( "     %s %s :%06u    \n%02u    ,1,%06u    ,1,%06u    ,1,%06u    ,1,%06u\n"
    payloadHeader = '{0}{1}:{2:>06d}\n{3:>02d},1,{4:>06d},1,{5:>06d},1,{6:>06d},1,{7:>06d}\n'.format(
        FRAME_HEADER,PROTOCOL_VERSION_NUMBER,payloadLen,4,len(jsonPart1),len(jsonPart2),len(jsonSysParams),len(jsonExecParams))
    strSerialised = payloadHeader + jsonPart1 + jsonPart2 + jsonSysParams + jsonExecParams
    return bytes( strSerialised, 'utf-8', 'ignore' )
    ##SerialiseToString

  # read the socket greeting for socket streams
  # @exception socket.error
  # https://docs.python.org/3/library/socket.html
  def ReadGreeting( self, s ):
    # read '024:'
    headerLen = s.recv( 4 )
    headerLen = headerLen[0:2]
    headerLen = int(headerLen)

    # read the rest of the greeting 'program@host pver x.x'
    greeting = s.recv( headerLen )
    match = re.search( r'pver ([\d.]+)', greeting )
    pver = None
    if match:
      pver = match.group(1)
    else:
      raise TxProcException( 'TxProc::ReadGreeting greeting "'+greeting+'" not recognised' )
    if pver != PROTOCOL_VERSION_NUMBER:
      raise TxProcException( 'TxProc::ReadGreeting greeting "'+greeting+'" - protocol version not supported - expecting:'+PROTOCOL_VERSION_NUMBER )
    return True
    ##ReadGreeting

  # read stream based reply
  # @exception socket.error
  # @return (0,failReason) or (1,'') for no response or (2,'') to expect a response
  def ReadReply( self, s ):
    (reply,err) = self.UnSerialise( s )
    if not bool(reply): return (0,err)
    return (2 if reply.BExpectReply() else 1, '')
    ##ReadReply
    
  # parses the body and populates the object
  # @return (1,'') or (0,errStr)
  def ParseBody( self, body ):
    numSections = 0
    part1Size = 0
    part2Size = 0
    sysSize = 0
    execSize = 0
    match = re.match( r'(\d+),1,(\d+),1,(\d+),1,(\d+),1,(\d+)', body )
    if match:
      numSections = int(match.group(1))
      part1Size = int(match.group(2))
      part2Size = int(match.group(3))
      sysSize = int(match.group(4))
      execSize = int(match.group(5))
      if numSections != 4: return (0, "TxProc::ParseBody: expected 4 sections found:"+str(numSections) )
      if part1Size == 0: return (0, "TxProc::ParseBody: part1 cannot be empty" )
    else:
      return (0,"TxProc::ParseBody: failed to parse body header:"+body)
    if self.beVerbose: log.info( '{} ParseBody part1Size:{} part2Size:{} sysSize:{} execSize:{}\nbody:{}'.format(Log.timestamp,part1Size,part2Size,sysSize,execSize,body) )

    # parse part1
    startOffset = BLOCK_HEADER_LEN
    jsonPart1 = body[startOffset:startOffset+part1Size]
    startOffset += part1Size
    self.part1 = json.loads( jsonPart1 )
    self.eventType = eEventType( self.part1['eventType'] )

    # parse part2
    if part2Size > 0:
      jsonPart2 = body[startOffset:startOffset+part2Size]
      startOffset += part2Size
      self.part2 = json.loads( jsonPart2 )

    # parse sysParams
    if sysSize > 0:
      jsonSysParams = body[startOffset:startOffset+sysSize]
      startOffset += sysSize
      self.sysParams = json.loads( jsonSysParams )

    # parse execParams
    if execSize > 0:
      jsonExecParams = body[startOffset:startOffset+execSize]
      startOffset += execSize
      self.execParams = json.loads( jsonExecParams )

    return (1,'')
  ##ParseBody

  # static method
  # expects the entire packet in the string
  # @return new object or (0,errStr)
  def UnSerialiseFromString( packet, beVerbose=False ):
    # extract the header
    if len(packet) < FRAME_HEADER_LEN: return (0,'TxProc::UnSerialiseFromString packet does not even contain the header')
    header = packet[0:FRAME_HEADER_LEN]

    # verify protocol version and extract payload length
    headerTemplate = '{}{}:(\\d+)'.format( FRAME_HEADER, PROTOCOL_VERSION_NUMBER )
    match = re.match( headerTemplate, header )
    if header:
      packetLen = match.group(1)
    else:
      return (0, "TxProc::UnSerialiseFromString failed to parse the header:'"+header+"'")

    # extract the body
    if len(packet) < FRAME_HEADER_LEN+packetLen: return (0,'TxProc::UnSerialiseFromString packet does contain the full body')
    body = packet[FRAME_HEADER_LEN:FRAME_HEADER_LEN+packetLen]

    # deserialise
    newObject = TxProc()
    if beVerbose: newObject.BeVerbose( True )
    (retVal,errStr) = newObject.ParseBody( body )
    if retVal:
      return (retVal,None)
    else:
      return (0,errStr)
    ##UnSerialiseFromString
  UnSerialiseFromString = staticmethod( UnSerialiseFromString )

  # static method
  # @exception socket.error
  # @return new object
  def UnSerialise( s, beVerbose=False ):
    log.debug( '{} UnSerialise type(s):{}'.format(Log.timestamp,type(s)) )
    # sockets and pipes are handled differently
    bPipe = True if isinstance(s,io.RawIOBase) else False;

    # read the header - the frame header, protocol version and payload length including the \n at the end
    if bPipe:
      header = str( s.read(FRAME_HEADER_LEN), encoding='utf-8', errors='ignore' )
    else:
      header = str( s.recv(FRAME_HEADER_LEN), encoding='utf-8', errors='ignore' )
    if not header: return (0, "eof" )
    if len(header) != FRAME_HEADER_LEN: return (0, "TxProc::UnSerialise only read "+str(len(header))+" bytes for the header" )

    # verify protocol version and extract payload length
    headerTemplate = '{}{}:(\d+)'.format( FRAME_HEADER, PROTOCOL_VERSION_NUMBER )
    match = re.match( headerTemplate, header )
    if match:
      packetLen = int( match.group(1) )
    else:
      log.debug( '{} UnSerialise failed to parse the match:{} header:{} template:{}'.format(Log.timestamp,match,header,headerTemplate) )
      return (0, "TxProc::UnSerialise failed to parse the header:"+str(header))

    # read the body
    body = ''
    partBody = None
    while len(body) < packetLen:
      if bPipe:
        partBody = str( s.read( packetLen-len(body) ), encoding='utf-8', errors='ignore' )
      else:
        partBody = str( s.recv( packetLen-len(body) ), encoding='utf-8', errors='ignore' )
      if not partBody: break
      body += partBody
    if partBody != None and partBody == '': return (0, "eof" )
    if len(body) != packetLen: return (0, "TxProc::UnSerialise read body len:"+str(packetLen)+" failed; read "+str(len(body))+" bytes" )

    # deserialise
    newObject = TxProc()
    if beVerbose: newObject.BeVerbose( True )
    (retVal,errStr) = newObject.ParseBody( body )
    if retVal:
      if beVerbose: log.debug( '{} UnSerialise new object:{}'.format(Log.timestamp,str(newObject)) )
      return (newObject,None)
    else:
      if beVerbose: log.debug( '{} UnSerialise failed err:{}'.format(Log.timestamp,errStr) )
      return (0,errStr)
    ##UnSerialise
  UnSerialise = staticmethod( UnSerialise )

  # @param s - if a socket object is passed in it is assumed it is a TCP/stream socket object
  # @param unixdomainPath - will create a SOCK_DGRAM - if a SOCK_STREAM is required create beforehand
  # @param serverName
  # @param serverService
  # @exception socket.error
  # @return ((0 fail, 1 success, 2 expect response),error string)
  def Serialise( self, s, unixdomainPath, serverName, serverService ):
    # serialise and get the actual payload
    payload = self.SerialiseToString()
    payloadLen = len(payload)
    if self.beVerbose: log.info( '{} Serialise len:{} {}'.format(Log.timestamp,payloadLen,payload) )

    # if we are restricted to a unix domain socket we cannot exceed the maximum length
    if unixdomainPath and payloadLen>READ_BUF_SIZE:
      if serverName and serverService:
        unixdomainPath = None
      else:
        return (0, "TxProc::Serialise len:"+str(payloadLen)+" exceeds max len:"+str(READ_BUF_SIZE)+" - no TCP server" )

    # if we were not given a socket object then create one
    retVal = 1
    errorString = None
    bStreamSocket = False
    bPipe = False
    bLocalSocketCreated = True
    if s:
      if s.type == socket.SOCK_STREAM: bStreamSocket = True
      bLocalSocketCreated = False
    else:
      if unixdomainPath:
        s = socket.socket( socket.AF_UNIX, socket.SOCK_DGRAM )
        s.connect( unixdomainPath )
      elif serverName and serverName=='-':
        bPipe = True
        s = os.dup(1)
      elif serverName and serverService:
        bStreamSocket = True
        for res in socket.getaddrinfo( serverName, serverService, socket.AF_UNSPEC, socket.SOCK_STREAM ):
          af, socktype, proto, _canonname, sa = res
          try:
            s = socket.socket( af, socktype, proto )
          except socket.error as msg:
            s = None
            continue
          try:
            s.connect( sa )
          except socket.error as msg:
            s.close()
            s = None
            continue
          break
        if not s: return (0, "TxProc::Serialise new TCP socket failed - server:"+serverName+", service:"+serverService+(' err:'+msg)if msg else '')
      else:
        return (0, "TxProc::Serialise no socket definitions available")
    log.debug( '{} Serialise s:{} retVal:{} errorString:{} bStreamSocket:{} bPipe:{} bLocalSocketCreated:{}'.format(Log.timestamp,str(s),retVal,errorString,bStreamSocket,bPipe,bLocalSocketCreated) )

    # if a stream socket first read the greeting
    if bStreamSocket:
      (retVal,errorString) = self.ReadGreeting(s)
      if not retVal:
        s.close()
        return (retVal,errorString)

    # send to the socket
    if bPipe:
      s.write( payload )
    else:
      s.sendall( payload )

    # read success or failure on submission and if we should wait for a response object
    # retVal is 0 for an error, 1 for no response and 2 for a response
    if bStreamSocket:
      (retVal,errorString) = self.ReadReply(s)
      
    # cleanup if required
    if bLocalSocketCreated:
      s.close()

    return(retVal,errorString)
  ##Serialise
##TxProc  
