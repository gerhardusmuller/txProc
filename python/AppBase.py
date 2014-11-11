# txProc persistent application base class
#
# Versioning: a.b.c a is a major release, b represents changes or new features, c represents bug fixes. 
# @version 1.0.0		07/11/2014		Gerhardus Muller		script created
#
# Copyright Gerhardus Muller
#
import json
import select
import os
import sys
import socket
import logging
import Log
import TxProc
from datetime import datetime
from TxProcException import TxProcException
from TxProc import TxProc,eEventType,eCommandType

# globals
log = None

class AppBase:
  # constructor
  def __init__(self,appName,logDir,bVerbose,bFlushLogs,buildStr):
    self.appName              = appName
    self.logDir               = logDir
    self.curMonth             = 0
    self.curDay               = 0
    self.curHour              = 0
    self.curMinute            = 0
    self.bRunMonthly          = False
    self.bRunDaily            = False
    self.bRunHourly           = False
    self.bRunMinutely         = False
    self.bDateRunSkipZero     = False
    self.bCheckDateChanges    = False
    self.bTimeToDie           = False
    self.bFrozen              = True
    self.ownQueue             = None
    self.workerPid            = -1
    self.resultEvent          = None
    self.buildString          = buildStr
    self.beVerbose            = bVerbose
    self.bMainLoopVerbose     = bVerbose
    self.bFlushLogs           = bFlushLogs
    self.txProcTcpAddr        = None
    self.txProcTcpService     = None
    self.txProcSocket         = None
    self.dateStringNow        = None

    self.logFile = logDir+appName+'.log'
    self.emergencyLog = '/tmp/'+appName+'.log'
    #print( 'AppBase::__init__ logFile:{}'.format(self.logFile) )
  ##__init__

  #######
  # open log file - use stderr for interactive usage
  def OpenAppLog( self, bUseStdErr, bReopen ):
    global log
    try:
      Log.OpenLog( self.logFile, bUseStdErr, self.bFlushLogs )
    except IOError as e:
      log = Log.OpenLog( self.emergencyLog, False, self.bFlushLogs )
      log.warn( '{} WARN OpenAppLog failed to write to main log:{} {} err:{}'.format(Log.timestamp,self.logFile),e )
      
    log = Log.GetLogger( __name__ )
    if not bReopen: log.info( "" )
    if not bReopen: log.info( "{} OpenAppLog: application started, frozen:{}".format(Log.timestamp,self.bFrozen) )
    if bReopen: log.info( "{} OpenAppLog: log reopened, frozen:{}".format(Log.timestamp,self.bFrozen) )
    if self.buildString: log.info( "{} OpenAppLog: buildString:{}".format(Log.timestamp,self.buildString) )
  ##OpenAppLog

  #######
  # user can override to do something useful
  def UserCmdReopenLog( self, _event, extraEvents ):
    return extraEvents
  ##UserCmdReopenLog

  #######
  # create the io support
  def SetupIoPoll( self ):
    # stdout buffering wont work on STDOUT
    sys.stdout = os.fdopen( sys.stdout.fileno(), 'wb', 0 )
    # stdin needs to be reopened as binary
    sys.stdin = os.fdopen( sys.stdin.fileno(), 'rb', 0 )

    # create a select object
    self.epoll = select.epoll()
    # add stdin by default
    self.epoll.register( sys.stdin.fileno(), select.EPOLLIN )
  ##SetupIoPoll

  #######
  # add an additional filehandle for event polling
  # @param ioHandle has to be fileno
  def AddIoPollFh( self, ioHandle ):
    self.epoll.register( ioHandle, select.EPOLLIN )
  ##AddIoPollFh

  #######
  # removes a filehandle from event polling
  # @param ioHandle has to be fileno
  def RemoveIoPollFh( self, ioHandle ):
    self.epoll.unregister( ioHandle )
  ##RemoveIoPollFh

  #######
  # creates a Unix Domain socket to submit events to txProc
  # it is normally a requirement for this function to succeed
  # @param txProcUdSocket - the txProc side
  # @param txProcTcpAddr - can be undefined
  # @param txProcTcpService - can be undefined
  # @exception on unable to unlink the local path, bind to it or to connect to the remote side
  def CreateTxProcSocket( self, txProcUdSocket, txProcTcpAddr, txProcTcpService ):
    localName = self.logDir+self.appName+".sock"
    try:
      os.unlink( localName )
    except OSError:
      if os.path.exists(localName): raise
    self.txProcSocket = socket.socket( socket.AF_UNIX, socket.SOCK_DGRAM )
    self.txProcSocket.bind( localName )
    self.txProcSocket.connect( txProcUdSocket )

    # we keep these in case we later have a packet that is to big to push over the UD socket
    self.txProcTcpAddr = txProcTcpAddr
    self.txProcTcpService = txProcTcpService
    log.info( '{} CreateTxProcSocket localName:{} peer:{} fallback TCP addr:{} service:{}'.format(Log.timestamp,localName,txProcUdSocket,txProcTcpAddr,txProcTcpService) )
  ##CreateTxProcSocket

  #######
  # main loop
  def Run( self ):
    while not self.bTimeToDie:
      Log.GenerateTimestamp()
      if self.bCheckDateChanges: self.CheckDateChanges()
      self.StartLoopProcess()
      if self.bMainLoopVerbose: log.info( '{} Run waiting for new packet/event frozen:{}'.format(Log.timestamp,self.bFrozen) )

      # wait for new events and execute any available
      extraEvents = []
      events = self.epoll.poll()
      for fh, event in events:
        txProcEvent = None
        if fh == sys.stdin.fileno():
          outputString = None
          try:
            (txProcEvent,err) = TxProc.UnSerialise( sys.stdin, True )
          except OSError as msg:
            log.warn( '{} Run TxProc.UnSerialise error:{}'.format(Log.timestamp,msg.strerror) )
          try:
            if not txProcEvent and err=='eof':
              log.info( '{} Run stdio eof'.format(Log.timestamp) )
              self.bTimeToDie = True
            elif txProcEvent:
              self.ConstructDefaultResultEvent( txProcEvent )
              extraEvents = self.HandleNewEvent( txProcEvent, extraEvents )
              outputString = self.SendDone()
            else:
              log.warn( '{} Run failed to successfully deserialise from stdin err:{}'.format(Log.timestamp,err) )
              self.resultEvent = TxProc( eEventType.EV_RESULT )
              self.resultEvent.BSuccess(0)
              self.resultEvent.ErrorString( 'AppBase::Run({}) failed to successfully deserialise TxProc object err:{}'.format(self.appName,err) )
              outputString = self.SendDone()
          except Exception as e:
            log.exception( '{} Run application exception err:{}'.format(Log.timestamp,str(e)) )
            self.resultEvent = TxProc( eEventType.EV_RESULT )
            self.resultEvent.BSuccess(0)
            self.resultEvent.ErrorString( 'AppBase::Run({}) application exception err:{}'.format(self.appName,str(e),err) )
            outputString = self.SendDone()

          # write a mandatory response
          if outputString: sys.stdout.write( outputString )
        else:
          try:
            extraEvents = self.HandlePolledFh( fh, extraEvents )
          except Exception as e:
            log.warn( '{} Run HandlePolledFh exception err:{}'.format(Log.timestamp,str(e)) )
        ##if/else fh == sys.stdin.fileno
      ##for fh

      # hook for maintenance events for which a timer runs that breaks the wait - we provide the hook here so 
      # that any events it produces are immediately dispatched
      if not self.bFrozen: extraEvents = self.ExecRegularTasks( extraEvents )
      
      # submit events generated by event handling functions
      # handle EV_BASE events internally by calling handleNewEvent directly
      # submit the rest directly to txProc
      try:
        newExtraEvents = []
        for event in extraEvents:
          if event.EventType() == eEventType.EV_BASE:
            try:
              newExtraEvents = self.HandleNewEvent( event, newExtraEvents )
            except Exception as e:
              log.warn( '{} Run HandleNewEvent 1 exception err:{}'.format(Log.timestamp,str(e)) )
            if newExtraEvents:
              extraEvents.append( newExtraEvents )
              newExtraEvents = []
          else:
            try:
              (retVal,errString) = event.Serialise( self.txProcSocket,None,self.txProcTcpAddr,self.txProcTcpService )
              if not retVal: log.warn( '{} Run HandleNewEvent Serialise failed err:{}'.format(Log.timestamp,errString) )
            except Exception as e:
              log.exception( '{} Run HandleNewEvent Serialise exception err:{}'.format(Log.timestamp,str(e)) )
          ##if/else event.EventType() == eEventType.EV_BASE
        ##for
      except Exception as e:
        log.exception( '{} Run extra event application exception:{}'.format(Log.timestamp,str(e)) )
    ##while not self.bTimeToDie
  ##Run

  ######
  ######
  # callbacks that can be overridden in a the derived class
  ######
  
  ######
  # called at the start of the loop before waiting for any new events
  # always runs
  def StartLoopProcess( self ):
    ...
  
  ######
  # called after startLoopProcess but only if not frozen
  def ExecRegularTasks( self, extraEvents ):
    return extraEvents

  ######
  # called if a file handle other than stdio is returned by the poll function
  # @param $fh - contains the IO::Handle that was passed to addIoPollFh
  # @return undef or a reference to an array of new events to be processed 
  def HandlePolledFh( self, fh, extraEvents ):
    self.RemoveIoPollFh( fh )
    log.warn( '{} HandlePolledFh unrecognised file handle:{} - removed from poll object'.format(Log.timestamp,fh) )
    return extraEvents

  ######
  # handle new application events received from txProc - do not normally override - rather implement ..
  # @param $event
  # @return undef or a reference to an array of new events to be processed 
  def HandleNewEvent( self, event, extraEvents ):
    if self.beVerbose: log.info( '{} HandleNewEvent:{}'.format(Log.timestamp,str(event)) )
    if event.EventType() == eEventType.EV_COMMAND:
      extraEvents = self.HandleEvCommand( event, extraEvents )
    elif event.EventType() == eEventType.EV_PERL:
      extraEvents = self.HandleEvPerl( event, extraEvents )
    elif event.EventType() == eEventType.EV_URL:
      extraEvents = self.HandleEvUrl( event, extraEvents )
    elif event.EventType() == eEventType.EV_RESULT:
      extraEvents = self.HandleEvResult( event, extraEvents )
    elif event.EventType() == eEventType.EV_BASE:
      extraEvents = self.HandleEvBase( event, extraEvents )
    elif event.EventType() == eEventType.EV_ERROR:
      extraEvents = self.HandleEvError( event, extraEvents )
    else:
      extraEvents = self.handleEvOther( event, extraEvents )
    return extraEvents
  ##HandleNewEvent

  ######
  # handles EV_COMMAND events. does not normally need to be overridden
  # it invokes HandlePersistentCommand,HandleUnhandledCmdEvents and indirectly HandleUserPersistentCommand
  # that would be more appropriate ot re-implement
  # @param $event
  def HandleEvCommand( self, event, extraEvents ):
    command = event.Command()
    if command == eCommandType.CMD_REOPEN_LOG:
      Log.CloseLog()
      self.OpenAppLog(0,1)
      extraEvents = self.UserCmdReopenLog( event, extraEvents )
    elif command == eCommandType.CMD_STATS:
      extraEvents = self.GenerateStats( event, extraEvents )
    elif command == eCommandType.CMD_EXIT_WHEN_DONE:
      self.bTimeToDie = True
      extraEvents = self.PrepareToExit( event, extraEvents )
    elif command == eCommandType.CMD_PERSISTENT_APP:
      extraEvents = self.HandlePersistentCommand( event, extraEvents )
    else:
      extraEvents = self.HandleUnhandledCmdEvents( event, extraEvents )
    return extraEvents
  ##HandleEvCommand

  #######
  # called for unhandled CMD_ events
  def HandleUnhandledCmdEvents( self, event, extraEvents ):
    log.info( '{} HandleUnhandledCmdEvents unable to handle command:{}'.format(Log.timestamp,event.Command()) )
    return extraEvents
  ##HandleUnhandledCmdEvents

  #######
  # handles persistent app command events - typically controlling/configuring the app
  # implement for customised behaviour - default behaviour can freeze / unfreeze and exit - typically to upgrade code or settings
  def HandlePersistentCommand( self, event, extraEvents ):
    cmd = event.GetParam('cmd')
    if not cmd:
      extraEvents = self.HandleUserPersistentCommand( event, extraEvents )
    elif cmd == 'stop':
      self.bFrozen = True
      log.info( '{} HandlePersistentCommand freezing execution'.format(Log.timestamp) )
    elif cmd == 'start':
      self.bFrozen = False
      log.info( '{} HandlePersistentCommand unfreezing execution'.format(Log.timestamp) )
    elif cmd == 'exit':
      self.bTimeToDie = True
      extraEvents = self.PrepareToExit( event, extraEvents )
      log.info( '{} HandlePersistentCommand exiting'.format(Log.timestamp) )
    elif cmd == 'startupinfo':
      self.ownQueue = event.GetParam('ownqueue')
      self.workerPid = event.GetParam('workerpid')
      log.info( '{} HandlePersistentCommand ownQueue:{} workerPid:{}'.format(Log.timestamp, self.ownQueue, self.workerPid) )
    else:
      extraEvents = self.HandleUserPersistentCommand( event, extraEvents )
    return extraEvents
  ##HandlePersistentCommand

  ######
  # handles user extensions to persistent commands
  # should be implemented to do something useful
  # the extensions are either additional 'cmd' values or events not using the 'cmd' syntax
  def HandleUserPersistentCommand( self, event, extraEvents ):
    log.info( '{} HandleUserPersistentCommand cannot handle event:{}'.format(Log.timestamp,str(event)) )
    extraEvents.append( self.CreateReturnEvent(event,None,'failed','no handler') )
    return extraEvents
  ##HandleUserPersistentCommand
  
  ######
  # handles EV_PERL events. implement to do something useful
  # @param event
  def HandleEvPerl( self, event, extraEvents ):
    log.info( '{} HandleEvPerl cannot handle event:{}'.format(Log.timestamp,str(event)) )
    extraEvents.append( self.CreateReturnEvent(event,None,'failed','no handler') )
    return extraEvents
  ##HandleEvPerl

  ######
  # handles EV_URL events. implement to do something useful
  # @param event
  def HandleEvUrl( self, event, extraEvents ):
    log.info( '{} HandleEvUrl cannot handle event:{}'.format(Log.timestamp,str(event)) )
    extraEvents.append( self.CreateReturnEvent(event,None,'failed','no handler') )
    return extraEvents
  ##HandleEvUrl

  ######
  # handles EV_RESULT events. implement to do something useful
  # @param event
  def HandleEvResult( self, event, extraEvents ):
    # EV_RESULT events are normally in response to a service request - cannot send a return event
    log.info( '{} HandleEvResult cannot handle event:{}'.format(Log.timestamp,str(event)) )
    return extraEvents
  ##HandleEvResult

  ######
  # handles EV_BASE events. implement to do something useful
  # @param event
  def HandleEvBase( self, event, extraEvents ):
    # EV_Base events are normally internal events - cannot send a return event
    log.info( '{} HandleEvBase cannot handle event:{}'.format(Log.timestamp,str(event)) )
    return extraEvents
  ##HandleEvBase

  ######
  # handles EV_BASE events. implement to do something useful
  # @param event
  def HandleEvError( self, event, extraEvents ):
    # EV_Base events are already error events - cannot send a return event
    log.info( '{} HandleEvError cannot handle event:{}'.format(Log.timestamp,str(event)) )
    return extraEvents
  ##HandleEvError

  ######
  # handles events other than EV_COMMAND,EV_PERL,EV_RESULT. implement to do something useful
  # @param event
  def HandleEvOther( self, event, extraEvents ):
    log.info( '{} HandleEvOther cannot handle event:{}'.format(Log.timestamp,str(event)) )
    extraEvents.append( self.CreateReturnEvent(event,None,'failed','no handler') )
    return extraEvents
  ##HandleEvOther

  #######
  def ConstructDefaultResultEvent( self, event ):
    self.resultEvent = TxProc( eEventType.EV_RESULT )
    self.resultEvent.BSuccess(1)
    self.resultEvent.Reference( event.Reference() )
    resultQueue = event.GetParam( 'resultQueue' )
    if resultQueue: self.resultEvent.DestQueue( resultQueue )
    self.resultEvent.AddParam( 'generatedby', self.appName )
    if self.beVerbose: log.info( '{} ConstructDefaultResultEvent event:{}'.format(Log.timestamp,str(self.resultEvent)) )
  ##ConstructDefaultResultEvent

  #######
  def SendDone( self ):
    if not self.resultEvent:
      self.resultEvent = TxProc( eEventType.EV_RESULT )
      self.resultEvent.BSuccess(1)
    resultString = self.resultEvent.SerialiseToString()
    self.resultEvent = None
    return resultString
  ##SendDone

  ######
  # implement to do something useful
  def GenerateStats( self, event, extraEvents ):
    return extraEvents
  ##GenerateStats

  ######
  # called on receiving CMD_EXIT_WHEN_DONE or a cmd:exit
  def PrepareToExit( self, event, extraEvents ):
    return extraEvents
  ##GenerateStats

  ######
  # utility functions
  ######

  #######
  def CreateReturnEvent( self, event, errType, status, error ):
    eventL = TxProc( eEventType.EV_RESULT )
    eventL.DestQueue( event.GetParam('resultQueue') )
    eventL.Result( status )
    eventL.BeVerbose( True )
    eventL.Reference( event.Reference() )
    if errType: eventL.AddParam( 'type', errType )
    if error: eventL.AddParam( 'error', error )
    eventL.AddParam( 'generatedby', self.appName )
    return eventL
  ##CreateReturnEvent

  #######
  def CheckDateChanges( self ):
    d = datetime.fromtimestamp( Log.epochSecs )
    self.dateStringNow = d.isoformat(' ')
    newMonth = d.month
    newDay = d.day
    newHour = d.hour
    newMinute = d.minute

    if newMinute != self.curMinute:
      log.info( '{} CheckDateChanges curMinute:{} newMinute:{}'.format(Log.timestamp,self.curMinute,newMinute) )
      self.curMinute = newMinute
      if not self.bDateRunSkipZero and self.curMinute != 0: self.bRunMinutely = True
    if newHour != self.curHour:
      log.info( '{} CheckDateChanges curHour:{} newHour:{}'.format(Log.timestamp,self.curHour,newHour) )
      self.curHour = newHour
      if not self.bDateRunSkipZero and self.curHour != 0: self.bRunHourly = True
    if newDay != self.curDay:
      log.info( '{} CheckDateChanges curDay:{} newDay:{}'.format(Log.timestamp,self.curDay,newDay) )
      self.curDay = newDay
      if not self.bDateRunSkipZero and self.curDay != 0: self.bRunDaily = True
    if newMonth != self.curMonth:
      log.info( '{} CheckDateChanges curMonth:{} newMonth:{}'.format(Log.timestamp,self.curMonth,newMonth) )
      self.curMonth = newMonth
      if not self.bDateRunSkipZero and self.curMonth != 0: self.bRunMonthly = True
  ##CheckDateChanges
##AppBase
