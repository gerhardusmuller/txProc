# general logging support
#
# Versioning: a.b.c a is a major release, b represents changes or new features, c represents bug fixes. 
# @version 1.0.0		06/11/2014		Gerhardus Muller		script created
#
# Copyright Gerhardus Muller
#
import time
import os
import sys
import logging
import logging.handlers

#globals
timestamp = ''
timeString = ''
logYear = None
logMonth = None
logDay = None
beVerbose = False
bUseStdErr = False
olderr = None
epochSecs = 0

def GenerateTimestamp():
  global epochSecs
  global logYear
  global logMonth
  global logDay
  global timeString
  global timestamp
  epochSecs = int(time.time())
  tm = time.localtime( epochSecs )
  logYear = tm.tm_year
  logMonth = tm.tm_mon
  logDay = tm.tm_mday
  timeString = '{:02d}{:02d}{:04d} {:02d}:{:02d}:{:02d}'.format( tm.tm_mday,tm.tm_mon,tm.tm_year,tm.tm_hour,tm.tm_min,tm.tm_sec )
  timestamp = '[{} {:06d}]'.format( timeString, os.getpid() )
  return timestamp
##GenerateTimestamp

# open the log and generate a date stamp
# @param logFile - if stderr then duplicate the STDERR handle
# @exception IOError on failure to open
# @return log
def OpenLog( logFile, pleaseUseStdErr=False, pleaseFlushLogs=False ):
  global fileHandler,bUseStdErr
  bFlushLogs = pleaseFlushLogs
  bUseStdErr = pleaseUseStdErr
  GenerateTimestamp()

  logging.captureWarnings( True )
  logging.addLevelName( 'WARN', 30 )
  logging.addLevelName( 'info', 20 )
  logging.addLevelName( 'debug', 10 )
  log = logging.getLogger()
  log.setLevel( logging.DEBUG )
  formatter = logging.Formatter( '{levelname:5s} {name:8s} {message}', style='{' )
  if bUseStdErr:
    fileHandler = logging.StreamHandler( sys.stderr )
  else:
    fileHandler = logging.handlers.WatchedFileHandler( logFile )
  fileHandler.setFormatter( formatter )
  log.addHandler( fileHandler )

  return log
##OpenLog

# rather use  log = logging.getLogger( __name__ )
def GetLogger( name ):
  return logging.getLogger( name )
##GetLogger

# closes the log - not normally required
def CloseLog():
  global fileHandler
  fileHandler.close()
  fileHandler = None
##CloseLog

# redirects STDERR to LOGFILE
def RedirectStdErr():
  global olderr
  olderr = sys.stdout
#  sys.stdout = LOGFILE
##RedirectStdErr

# restores STDERR again
def RestoreStdErr():
  ...
#  sys.stdout = olderr
##RestoreStdErr
