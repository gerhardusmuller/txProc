# test AppBase class
#
# Versioning: a.b.c a is a major release, b represents changes or new features, c represents bug fixes. 
# @version 1.0.0		10/11/2014		Gerhardus Muller		script created
#
# Copyright Gerhardus Muller
#
import os
import sys
import stat
import logging
import Log
import conf.ConfigTest as cnf
from datetime import datetime
from TxProcException import TxProcException
from TxProc import TxProc,eEventType,eCommandType
from AppBase import AppBase

log = Log.GetLogger( __name__ )

class TestApp(AppBase):
  # constructor
  def __init__(self,bVerbose,bFlushLogs,bQuiet,bUseStdErr):
    # come up with a buildtime - take it to be the last modification time of this file
    # may cause trouble if only distributing the cached files
    buildtime = None
    selfPath = os.path.abspath(__file__)
    if os.path.exists(selfPath):
      st = os.stat( selfPath )
      buildtime = str(datetime.fromtimestamp(st.st_mtime))

    AppBase.__init__( self, 'test', cnf.logDir, bVerbose, bFlushLogs, buildtime )

    # open logs
    self.OpenAppLog( bUseStdErr, 0 )
    log.debug( '{} best case logger - name:{} path:{} time:{}'.format(Log.timestamp,__name__,selfPath,buildtime) )

    # create the stdio select / poll object
    self.SetupIoPoll()

    # create the unix domain socket to txProc
    if not bUseStdErr: 
      try:
        self.CreateTxProcSocket( cnf.txProcUdSocket, cnf.txProcTCPIP, cnf.txProcTCPService )
      except Exception as e:
        sys.exit( '{} failed to create socket to txProc: {}\n'.format(self.__class__, e) )

    # we have maintenance tasks to run
    self.bCheckDateChanges = True
  ##__init__
##TestApp

