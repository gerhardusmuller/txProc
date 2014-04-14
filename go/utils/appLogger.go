// appLogger provides application logging services - mainly in creating the log file and supporting log rotation
//
// Versioning: a.b.c a is a major release, b represents changes or new features, c represents bug fixes.
// @version 1.0.0   09/04/2014    Gerhardus Muller     Script created
//
// @note
//
// @todo
//
// @bug
//
// Copyright Gerhardus Muller

package appLogger

import (
	"log"
	"os"
  "fmt"
)

var Log *log.Logger
var fd *os.File
var fname string

func NewLogger(logname string) error {
  if logname=="stderr" {
    Log = log.New( os.Stderr, "", log.LstdFlags )
  } else {
  	fname = logname
  	var err error
  	if fd,err = os.OpenFile(logname, os.O_CREATE|os.O_APPEND|os.O_APPEND|os.O_WRONLY, 0666); err != nil {
      fmt.Fprintf( os.Stderr, "ERROR NewLogger logname:'%s' failed - %s\n", logname, err )
  		return err
  	}
  	Log = log.New( fd, "", log.LstdFlags )
  } // else 
  Log.Print( "=== application started ===" )
	return nil
} // NewLogger

func Close() error {
  Log.Print( "=== application exited ===" )
  if fd != nil {
  	err := fd.Close()
    return err
  } // if
  return nil
} // Close

func Reopen() error {
  if fd != nil {
  	if err := fd.Close(); err != nil { return err }
  	return NewLogger(fname)
  }
  return nil
} // Close
