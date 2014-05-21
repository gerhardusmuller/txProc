// appBase is the base class for a txProc persistent application
// 
// Versioning: a.b.c a is a major release, b represents changes or new features, c represents bug fixes. 
// @version 1.0.0   17/04/2014    Gerhardus Muller     Script created
//
// @note
//
// @todo 
//
// http://stackoverflow.com/questions/18267460/golang-goroutine-pool
// http://www.golang-book.com/10
// 
// @bug
//
// Copyright Gerhardus Muller

package appBase

import (
  "fmt"
  "os"
  "net"
  "io"
  "strconv"
  "time"
  "strings"
  . "github.com/gerhardusmuller/txProc/go/baseEvent"
  . "github.com/gerhardusmuller/txProc/go/utils"
)

type ChanEvent struct {
  event                   *BaseEvent
  err                     error
  chanId                  int
}

type AppBase struct {
  appName                 string
  logDir                  string
  logFile                 string
  emergencyLog            string
  curMonth                time.Month
  curDay                  int
  curHour                 int
  curMinute               int
  bRunMonthly             bool
  bRunDaily               bool
  bRunHourly              bool
  bRunMinutely            bool
  bDateRunSkipZero        bool
  bCheckDateChanges       bool
  bLogOpen                bool
  bTimeToDie              bool
  bFrozen                 bool
  ownQueue                string
  workerPid               uint
  resultEvent             *BaseEvent
  buildString             string
  beVerbose               bool
  bMainLoopVerbose        bool
  bFlushLogs              bool
  errString               string
  eventSrc                chan ChanEvent
  srcIdCounter            int                   // used to generate sequential ids if more sources inject events into the eventSrc channel
  txProcSocket            *net.UnixConn         // UD datagram socket to txProc
  txProcStreamSocket      *net.UnixConn         // UD stream socket to txProc
} // type AppBase struct

const ID_STDIO = 0                              // id for events from stdio

/**
 * initialisation - call in sequence
 *  Init
 *  createTxProcSocket
 * **/
func (s *AppBase) Init( appName, logDir string, bUseStdErr, bVerbose, bFlushLogs bool, buildStr string ) (err error) {
  s.appName = appName
  s.logDir = logDir
  s.beVerbose = bVerbose
  s.bMainLoopVerbose = bVerbose
  s.buildString = buildStr

  if !strings.HasSuffix( s.logDir, "/" ) { s.logDir += "/" }

  // set logging up
  s.logFile = s.logDir + appName + ".log"
  s.emergencyLog = "tmp/" + appName + ".log";
  err = s.openAppLog( bUseStdErr, false )

  // create the channel on which to wait for events
  s.eventSrc = make( chan ChanEvent )

  // create the stdin io go-routine
  go readStdIn( s.eventSrc, s.GetNextEventSrcId(), &s.bTimeToDie )
  return
} // Init

/**
 * returns sequential src ids for the eventSrc channel
 * **/
func (s *AppBase) GetNextEventSrcId() int {
  id := s.srcIdCounter
  s.srcIdCounter++
  return id
} // AppBase GetNextEventSrcId

/**
 * creates a UD datagram and stream socket to txProc
 * it is normally a requirement for this function to succeed
 * datagramPath and streamPath are the respective paths on which txProc listens
 * if bAppendPid is true the process pid becomes part of the local names
 * error should be nil for success
 * **/
func (s *AppBase) CreateTxProcSocket( datagramPath, streamPath string, bAppendPid bool ) error {
  localDgramPath := s.logDir + s.appName 
  if bAppendPid { localDgramPath += "." + strconv.FormatInt( int64(os.Getpid()), 10 ) }
  localDgramPath += ".sock";
  localStreamPath := s.logDir + s.appName 
  if bAppendPid { localStreamPath += "." + strconv.FormatInt( int64(os.Getpid()), 10 ) }
  localStreamPath += "stream.sock";

  rDgramAddr := net.UnixAddr{datagramPath,"unixgram"}
  lDgramAddr := net.UnixAddr{localDgramPath,"unixgram"}
  rStreamAddr := net.UnixAddr{streamPath,"unix"}
  lStreamAddr := net.UnixAddr{localStreamPath,"unix"}

  var err error
  if s.txProcSocket,err = net.DialUnix("unixgram", &lDgramAddr, &rDgramAddr); err != nil {
    Log.Print( "WARN AppBase::createTxProcSocket failed to dial unixgram local:",localDgramPath," remote:",datagramPath," err:",err )
    return err
  } // if
  if s.txProcStreamSocket,err = net.DialUnix("unix", &lStreamAddr, &rStreamAddr); err != nil {
    Log.Print( "WARN AppBase::createTxProcSocket failed to dial unix local:",localStreamPath," remote:",streamPath," err:",err )
    return err
  } // if

  Log.Print( "info  AppBase::createTxProcSocket unixgram local:",localStreamPath," remote:",streamPath," unixstream local:",localStreamPath," remote:",streamPath )
  return nil
} // AppBase::createTxProcSocket


/**
 * interfaces - can be implemented by an object
 * **/

/**
 * log extensions
 * **/
type LogExtensionsIf interface {
  UserCmdReopenLog()
} // interface LogExtensionsIf

/**
 * optional - can be implemented to do something useful
 * **/
func (s *AppBase) UserCmdReopenLog() {
} // AppBase UserCmdReopenLog

/**
 * functions called as part of loop execution but unrelated to event handling
 * **/
type LoopTasksIf interface {
  StartLoopProcess()
  ExecRegularTasks() []*BaseEvent
} // interface LoopTasksIf

/**
 * called at the start of the loop before waiting for any new events
 * **/
func (s *AppBase) StartLoopProcess() {
} // AppBase StartLoopProcess

/**
 * called after processing new stdin events but only if not frozen
 * return a slice of new events to be processed 
 * **/
func (s *AppBase) ExecRegularTasks() []*BaseEvent {
  return nil
} // AppBase::ExecRegularTasks

/**
 * additional event sources
 * **/
type AdditionalEventSourcesIf interface {
  HandlePolledFh( packet *ChanEvent ) []*BaseEvent
} // interface AdditionalEventSourcesIf

/**
 * handles events with custom origination ids (not ID_STDIO)
 * needs to be implemented to do anything useful
 * return a slice of new events to be processed 
 * **/
func (s *AppBase) HandlePolledFh( packet *ChanEvent ) []*BaseEvent {
  Log.Print( "info  AppBase::HandlePolledFh not implemented" )
  return nil
} // AppBase::HandlePolledFh

/**
 * application process methods
 * **/
type ApplicationProcessIf interface {
  PrepareToExit( event *BaseEvent ) []*BaseEvent
} // interface ApplicationProcessIf

/**
 * called on receiving CMD_EXIT_WHEN_DONE after setting bTimeToDie to true
 * **/
func (s *AppBase) PrepareToExit( event *BaseEvent ) []*BaseEvent {
  return nil
} // AppBase::PrepareToExit

/**
 * should be overridden to do something useful
 * **/
func (s *AppBase) GenerateStats( event *BaseEvent ) []*BaseEvent {
  return nil
} // AppBase::GenerateStats

/**
 * event handling and processing interfaces
 * **/
type EventHandlingIf interface {
  HandleNewEvent( event *BaseEvent ) []*BaseEvent
  HandleEvCommand( event *BaseEvent ) []*BaseEvent
  HandleEvPerl( event *BaseEvent ) []*BaseEvent
  HandleEvUrl( event *BaseEvent ) []*BaseEvent
  HandleEvResult( event *BaseEvent ) []*BaseEvent
  HandleEvBase( event *BaseEvent ) []*BaseEvent
  HandleEvError( event *BaseEvent ) []*BaseEvent 
  HandleEvOther( event *BaseEvent ) []*BaseEvent
  HandlePersistentCommand( event *BaseEvent ) []*BaseEvent
  HandleUnhandledCmdEvents( event *BaseEvent ) []*BaseEvent
  HandleUserPersistentCommand( event *BaseEvent ) []*BaseEvent
} // interface EventHandlingIf

/**
 * main event handler for stdio received events
 * return a slice of new events to be processed 
 * **/
func (s *AppBase) HandleNewEvent( event *BaseEvent ) []*BaseEvent {
  Log.Print( "info  AppBase::HandleNewEvent: ", event.GoString() )

  switch event.EventType() {
  case EV_COMMAND:  return s.HandleEvCommand( event )
  case EV_PERL:     return s.HandleEvPerl( event )
  case EV_URL:      return s.HandleEvUrl( event )
  case EV_RESULT:   return s.HandleEvResult( event )
  case EV_BASE:     return s.HandleEvBase( event )
  case EV_ERROR:    return s.HandleEvError( event )
  default:          return s.HandleEvOther( event )
  } // switch
} // AppBase::HandleNewEvent

/**
 * handles EV_COMMAND events. does not normally need to be overridden
 * it invokes handlePersistentCommand,handleUnhandledCmdEvents and indirectly handleUserPersistentCommand
 * that would be more appropriate ot re-implement
 * **/
func (s *AppBase) HandleEvCommand( event *BaseEvent ) []*BaseEvent {
  var newEvents []*BaseEvent
  command := event.Command()
  Log.Print( "info  AppBase::HandleEvCommand:", command )

  switch command {
  case CMD_REOPEN_LOG:
    LoggerClose()
    s.openAppLog( false, true )
    s.UserCmdReopenLog()
  case CMD_STATS:
    newEvents = s.GenerateStats( event )
  case CMD_EXIT_WHEN_DONE:
    Log.Print( "info  AppBase::HandleEvCommand bTimeToDie" )
    s.bTimeToDie = true
    newEvents = s.PrepareToExit( event )
  case CMD_PERSISTENT_APP:
    newEvents = s.HandlePersistentCommand( event )
  default:
    newEvents = s.HandleUnhandledCmdEvents( event )
  } // switch

  return newEvents
} // AppBase::HandleEvCommand

/**
 *  handles EV_PERL events. implement to do something useful
 * **/
func (s *AppBase) HandleEvPerl( event *BaseEvent ) []*BaseEvent {
  Log.Print( "debug AppBase::HandleEvPerl - unhandled" )
  newEvents := make( []*BaseEvent, 1, 1 )
  newEvents[0] = s.createReturnEvent( event, "", "failed", "no handler" )
  return newEvents
} // AppBase::HandleEvPerl

/**
 * handles EV_URL events. implement to do something useful
 * **/
func (s *AppBase) HandleEvUrl( event *BaseEvent ) []*BaseEvent {
  Log.Print( "debug AppBase::HandleEvUrl - unhandled" )
  newEvents := make( []*BaseEvent, 1, 1 )
  newEvents[0] = s.createReturnEvent( event, "", "failed", "no handler" )
  return newEvents
} // AppBase::HandleEvUrl

/**
 * handles EV_RESULT events. implement to do something useful
 * **/
func (s *AppBase) HandleEvResult( event *BaseEvent ) []*BaseEvent {
  Log.Print( "debug AppBase::HandleEvResult - unhandled" )
  // normally a response to a service request - cannot send a return event
  return nil
} // AppBase::HandleEvResult

/**
 * handles EV_BASE events - these are normally internal to the application. implement to do something useful
 * **/
func (s *AppBase) HandleEvBase( event *BaseEvent ) []*BaseEvent {
  Log.Print( "debug AppBase::HandleEvBase - unhandled" )
  // EV_BASE are application internal - cannot send a return event
  return nil
} // AppBase::HandleEvBase

/**
 * handles EV_ERROR events - these are produced by txProc on failure to handle an event. implement to do something useful
 * **/
func (s *AppBase) HandleEvError( event *BaseEvent ) []*BaseEvent {
  Log.Print( "debug AppBase::HandleEvError - unhandled" )
  // EV_ERROR are already error events - cannot send a return event
  return nil
} // AppBase::HandleEvError

/**
 * handles events other than EV_COMMAND,EV_PERL,EV_RESULT. implement to do something useful
 * **/
func (s *AppBase) HandleEvOther( event *BaseEvent ) []*BaseEvent {
  Log.Print( "debug AppBase::HandleEvOther - unhandled" )
  newEvents := make( []*BaseEvent, 1, 1 )
  newEvents[0] = s.createReturnEvent( event, "", "failed", "no handler" )
  return newEvents
} // AppBase::HandleEvOther


/**
 * handles persistent app command events - typically controlling/configuring the app
 * implement for customised behaviour - default behaviour can freeze / unfreeze and exit - typically to upgrade code or settings
 * **/
func (s *AppBase) HandlePersistentCommand( event *BaseEvent ) []*BaseEvent {
  var newEvents []*BaseEvent

  if !event.ExistsParam("cmd") {
    newEvents = s.HandleUnhandledCmdEvents( event )
  } else {
    cmd,_ := event.GetParamAsStr("cmd")
    switch cmd {
    case "stop":
      s.bFrozen = true
      Log.Print( "info  AppBase::HandlePersistentCommand freezing execution" )
    case "start":
      s.bFrozen = false
      Log.Print( "info  AppBase::HandlePersistentCommand unfreezing execution" )
    case "exit":
      Log.Print( "info  AppBase::HandlePersistentCommand bTimeToDie" )
      s.bTimeToDie = true
      newEvents = s.PrepareToExit( event )
    case "startupinfo":
      s.ownQueue,_ = event.GetParamAsStr("ownqueue")
      s.workerPid,_ = event.GetParamAsUint("workerpid")
      Log.Printf( "info  AppBase::HandlePersistentCommand ownQueue:%s workerPid:%v", s.ownQueue, s.workerPid )
    default:
      newEvents = s.HandleUnhandledCmdEvents( event )
    } // switch
  } // else

  return newEvents
} // AppBase::HandlePersistentCommand

/**
 * called for unhandled CMD_ events
 * **/
func (s *AppBase) HandleUnhandledCmdEvents( event *BaseEvent ) []*BaseEvent {
  Log.Print( "debug AppBase::HandleUnhandledCmdEvents - unhandled" )
  return nil
} // AppBase::HandleUnhandledCmdEvents

/**
 * handles user extensions to persistent commands - should be implemented to do something useful
 * the extensions are either additional 'cmd' values or events not using the 'cmd' syntax
 * **/
func (s *AppBase) HandleUserPersistentCommand( event *BaseEvent ) []*BaseEvent {
  Log.Print( "debug AppBase::HandleUserPersistentCommand - unhandled" )
  return nil
} // AppBase::HandleUserPersistentCommand


/**
 * implementation of interfaces for default behaviour
 * **/

/**
 * main processing loop - should always be running
 * Run is defined a method (if my terminology is correct) with receiver s *AppBase to 
 * allow us to retrieve our 'own' struct that would be an anonymous member of the super class
 * appInt is the anonymous interface from which all our interfaces are derived.  it is 
 * typically on invocation exactly the same value as s - it is typically invoked as:
 * app.Run( app )
 * where app is an instance of the struct that contains AppBase as an anonymous field
 * this strange construct for which there is almost certainly a better solution allows
 * us to implement what subtyping polymorphism would have given us.  another solution would 
 * have been getter / setter methods in another interface
 * although not elegant this works - we can implement any of the methods in the super class for overriding behaviour
 * **/
func (s *AppBase) Run( appInt interface{} ) {
  // retrieve interface pointers
  iLoop := appInt.(LoopTasksIf)                     // StartLoopProcess, ExecRegularTasks
  iAddEvents := appInt.(AdditionalEventSourcesIf)   // HandlePolledFh
  iEventHandling := appInt.(EventHandlingIf)        // HandleNewEvent

  extraEvents := make( []*BaseEvent, 0, 5 )
  var outputString string
  Log.Print( "info  AppBase::Run" )

  // main processing loop
  for !s.bTimeToDie {
    // house keeping
    if s.bCheckDateChanges {s.checkDateChanges()}
    iLoop.StartLoopProcess();

    if s.bMainLoopVerbose { Log.Print("info  AppBase::Run waiting for new packet/event frozen:", s.bFrozen) }

    // we are dependent on txProc timer messages to break the infinite wait here if we are required to do housekeeping
    // we could also implement a maintInterval like the C++ with a select and case <- time.After(time.Second):
    // check for eof - we return a nil event
    eventPacket := <- s.eventSrc
    if eventPacket.chanId == ID_STDIO {
      if eventPacket.event == nil {
        if eventPacket.err == io.EOF {
          Log.Print( "info  AppBase::Run channel EOF - exiting" )
          s.bTimeToDie = true
          } else {
            Log.Print( "WARN AppBase::Run event deserialise error:", eventPacket.err )
            s.constructFailResultEvent( eventPacket.err.Error() )
            outputString = s.sendDone()
          } // else
      } else {
        // otherwise process event
        s.constructDefaultResultEvent( eventPacket.event )
        newEvents := iEventHandling.HandleNewEvent( eventPacket.event )
        if newEvents != nil { extraEvents = append( extraEvents, newEvents... ) }

        // write a mandatory response
        outputString = s.sendDone()
      } // else

      // os.Stdout.Write is by default unbuffered
      if _,err := os.Stdout.Write([]byte(outputString)); err != nil {
        Log.Print( "WARN AppBase::Run writing to stdout error - terminating - ", err )
        s.bTimeToDie = true
      } // if
    } else { // if ID_STDIO
      // the new event originates from somewhere other than stdin
      newEvents := iAddEvents.HandlePolledFh( &eventPacket )
      if newEvents != nil { extraEvents = append( extraEvents, newEvents... ) }
    } // else ID_STDIO

    // handle maintenance tasks
    if !s.bFrozen {
      newEvents := iLoop.ExecRegularTasks()
      if newEvents != nil { extraEvents = append( extraEvents, newEvents... ) }
    } // if

    // submit extra events directly to txProc with the exception of EV_BASE for
    // which HandleNewEvent is invoked directly
    if len(extraEvents) > 0 {
      for i := 0; i < len(extraEvents); i++ {
        event := extraEvents[i]
Log.Print( "debug AppBase::Run extra event:", event )
        if event.EventType() == EV_BASE {
          newEvents := s.HandleNewEvent( event )
          if newEvents != nil { extraEvents = append( extraEvents, newEvents... ) }
        } else {
          // give back to txProc
          if retVal,err := event.SerialiseToTxProc(s.txProcSocket, s.txProcStreamSocket); retVal==0||err!=nil {
            Log.Printf( "WARN AppBase::Run SerialiseToTxProc retVal:%d error:%s", err )
          } else {
            Log.Print( "info  AppBase::Run SerialiseToTxProc:", event.String() )
          } // else SerialiseToTxProc
        } // else EV_BASE
      } // for

      extraEvents = make( []*BaseEvent, 0, 5 )
    } // if len(extraEvents
  } // for !s.bTimeToDie
} // func AppBase::Run


/**
 * private functions
 * **/

/**
 * open log file - use stdout for interactive usage
 * refuses to continue if opening the main log and the emergency log fails
 * **/
func (s *AppBase) openAppLog( bUseStdErr, bReopen bool ) error {
  if bUseStdErr { s.logFile = "stderr" }
  if err:=NewLogger(s.logFile,s.bFlushLogs); err!=nil {
    if err1:=NewLogger(s.emergencyLog,s.bFlushLogs); err1!=nil {
      fmt.Print( "AppBase::openAppLog main exiting - failed to open log:", s.logFile, " - ", err, " and emergency log:", s.emergencyLog, " - ", err1 )
      os.Exit(1)
    } // if
    Log.Print( "info  AppBase::openAppLog: failed to write to main log:", s.logFile, " - ", err )
  } // if
  if !bReopen { Log.Print( "info  AppBase::openAppLog: application started, frozen:", s.bFrozen ) }
  if bReopen  { Log.Print( "info  AppBase::openAppLog: log reopened, frozen:", s.bFrozen ) }
  if len(s.buildString)>0 { Log.Print( "info  AppBase::openAppLog: buildString:", s.buildString ) }
  return nil
} // AppBase openAppLog

/**
 * flags the start of a new hour / day / month
 * **/
func (s *AppBase) checkDateChanges() {
  now := time.Now()
  newHour,newMinute,_ := now.Clock()
  _,newMonth,newDay := now.Date()

  if newMinute != s.curMinute {
    Log.Printf( "info  AppBase::checkDateChanges curMinute:%d newMinute:%d", s.curMinute, newMinute )
    s.curMinute = newMinute
    if !s.bDateRunSkipZero || (s.curMinute!=0) { s.bRunMinutely = true }
  } // if minute
  if newHour != s.curHour {
    Log.Printf( "info  AppBase::checkDateChanges curHour:%d newHour:%d", s.curHour, newHour )
    s.curHour = newHour
    if !s.bDateRunSkipZero || (s.curHour!=0) { s.bRunHourly = true }
  } // if hour
  if newDay != s.curDay {
    Log.Printf( "info  AppBase::checkDateChanges curDay:%d newDay:%d", s.curDay, newDay )
    s.curDay = newDay
    if !s.bDateRunSkipZero || (s.curDay!=0) { s.bRunDaily = true }
  } // if day
  if newMonth != s.curMonth {
    Log.Printf( "info  AppBase::checkDateChanges curMonth:%v newMonth:%v", s.curMonth, newMonth )
    s.curMonth = newMonth
    s.bRunMonthly = true
  } // if month
} // AppBase checkDateChanges

/**
 * go routine to read new events from stdin
 * **/
func readStdIn( eventSrc chan<- ChanEvent, id int, bTimeToDie *bool ) {
  var packet ChanEvent
  for !*bTimeToDie {
    event,err := UnSerialiseFromSocket( os.Stdin, true )
    if err != nil {
      Log.Print( "info  AppBase::readStdIn err:", err )
      packet = ChanEvent{nil,err,id}
    } else {
      packet = ChanEvent{event,err,id}
    } // else
    eventSrc <- packet
  } // for
} // readStdIn

/**
 * creates a return EV_RESULT TxProc event
 * **/
func (s *AppBase) createReturnEvent( event *BaseEvent, typeStr, statusStr, errorStr string ) *BaseEvent {
  eventL,_ := NewBaseEvent( EV_RESULT )
  eventL.SetDestQueue( event.DestQueue() )
  eventL.SetReference( event.Reference() )
  eventL.SetResult( statusStr )
  eventL.AddParamStr("generatedby", s.appName);
  if len(typeStr)>0 { eventL.AddParamStr("type", typeStr) }
  if len(errorStr)>0 { eventL.AddParamStr("error", errorStr) }
  return eventL
} // AppBase createReturnEvent

/**
 * creates the mandatory default result event
 * **/
func (s *AppBase) constructDefaultResultEvent( event *BaseEvent ) {
  var err error
  s.resultEvent,err = NewBaseEvent( EV_RESULT )
  if err != nil { Log.Print( "WARN AppBase::constructDefaultResultEvent NewBaseEvent error:", err ); return }
  s.resultEvent.SetBSuccess( true )
  s.resultEvent.SetReference( event.Reference() )
  if resultQueue,err := event.GetParamAsStr("resultQueue"); err == nil {
    s.resultEvent.SetDestQueue( resultQueue )
  } // if
  s.resultEvent.AddParamStr( "generatedby", s.appName )
  Log.Print( "debug AppBase::constructDefaultResultEvent: ", s.resultEvent.String() )
} // func AppBase::constructDefaultResultEvent

/**
 * constructs a basic result event indicating failure
 * **/
func (s *AppBase) constructFailResultEvent( errorString string ) {
  s.resultEvent,_ = NewBaseEvent( EV_RESULT )
  s.resultEvent.SetBSuccess( false )
  s.resultEvent.SetErrorString( errorString )
} // func AppBase::constructFailResultEvent

/**
 * serialises the result - if none exists it creates a default
 * **/
func (s *AppBase) sendDone() string {
  var err error
  if s.resultEvent == nil {
    Log.Print( "debug AppBase::sendDone creating result event" )
    s.resultEvent,_ = NewBaseEvent( EV_RESULT )
    s.resultEvent.SetBSuccess( true )
  } // if
  
  var strEvent string
  if strEvent,err = s.resultEvent.SerialiseToString(); err!=nil { Log.Print( "WARN AppBase::sendDone failed to serialise: ", s.resultEvent.String(), " err - ", err ) }
  s.resultEvent = nil
  return strEvent
} // func AppBase::sendDone

