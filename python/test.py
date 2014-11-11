#!/usr/bin/python3
# test txProc persistent app
#
# Versioning: a.b.c a is a major release, b represents changes or new features, c represents bug fixes. 
# @version 1.0.0		10/11/2014		Gerhardus Muller		script created
#
# Copyright Gerhardus Muller
#
import sys
import getopt
import signal
import Log
from TxProcException import TxProcException
from TxProc import TxProc,eEventType,eCommandType
from TestApp import TestApp
import conf.ConfigTest as cnf

def sigHandler( signum, frame ):
  test.bTimeToDie = True

######
# command line parameters
bHelp = False
bUseStdErr = False
beVerbose = False
myopts, args = getopt.getopt( sys.argv[1:],"hsv" )
for o,a in myopts:
  if o == '-h':
    bHelp = True
  elif o == '-s':
    bUseStdErr = True
  elif o == '-v':
    beVerbose = True

if bHelp:
  print( '{} options'.format(sys.argv[0]) )
  print( '  -h this help screen' )
  print( '  -s log against stderr' )
  print( '  -v switch to verbose mode' )
  sys.exit()

# handle signals properly
signal.signal( signal.SIGINT, signal.SIG_IGN )
signal.signal( signal.SIGTERM, sigHandler )

# run the main loop
test = TestApp( beVerbose, beVerbose, False, bUseStdErr )
log = Log.GetLogger( __name__ )
log.debug( '{} Hello I am running'.format(Log.timestamp) )
test.Run()
log.debug( '{} main: done'.format(Log.timestamp) )

