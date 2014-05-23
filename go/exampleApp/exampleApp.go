package main

// go get ./
// go build ./ && ./exampleApp
// TODO cleanup of UD socket paths

import (
  "os"
  "fmt"
  . "github.com/gerhardusmuller/txProc/go/exampleApp/conf"
  "github.com/gerhardusmuller/txProc/go/appBase"
  . "github.com/gerhardusmuller/txProc/go/utils"
//  . "github.com/gerhardusmuller/txProc/go/baseEvent"
)

type testApp struct {
  appBase.AppBase
  colour    string
} // PartDev1

const appName = "exampleApp"

func main() {
  // create a conf instance 
  if _,err := NewConfig(appName); err != nil { fmt.Fprint( os.Stderr, "WARN main NewConfig returned error:", err); os.Exit(1) }

  // create an instance of the application
  app := new( testApp )
  err := app.Init( appName, "/var/log/txProc", false, true, true, "1.13" )
  if err != nil { Log.Print( "WARN main app.Init returned error:", err); os.Exit(1) }
  defer func() { LoggerClose() }()
  Log.Print( "info main Config:", *Conf )
  err = app.CreateTxProcSocket( Conf.TxProcUdDatagramPath, Conf.TxProcUdStreamPath, true )
  if err != nil { Log.Print( "WARN main app.createTxProcSocket returned error:", err); os.Exit(1) }
  defer app.CleanupTxProcSocket()

  Log.Print( "info  test.appBase starting" )
  app.Run( app )
  Log.Print( "info  test.appBase done" )
} // func main

//func (s *testApp) HandleNewEvent( event *BaseEvent ) []*BaseEvent {
//  Log.Print( "info  testApp::HandleNewEvent: ", event.GoString() )
//  return nil
//} // AppBase::HandleNewEvent
//func (s *testApp) HandleNewEvent1( event *BaseEvent ) []*BaseEvent {
//  Log.Print( "info  testApp::HandleNewEvent1: ", event.String() )
//  return nil
//} // AppBase::HandleNewEvent

