# txProc persistent application base class
#
# $Id$
# Gerhardus Muller
#
# Versioning: a.b.c a is a major release, b represents changes or new features, c represents bug fixes. 
# @version 1.0.0		05/07/2010		Gerhardus Muller		script created
# @version 1.1.0		18/01/2011		Gerhardus Muller		added a handler for EV_ERROR
# @version 1.2.0		22/02/2012		Gerhardus Muller		added a resultEvent
# @version 1.2.1		03/03/2012		Gerhardus Muller		various bug fixes
# @version 1.3.0		19/04/2012		Gerhardus Muller		support for a build string in the startup message
# @version 1.4.0		05/05/2012		Gerhardus Muller		errorhandling to use existing resultevent
# @version 1.5.0    21/08/2012    Gerhardus Muller    support for a startup info command event
# @version 1.6.0    27/09/2012    Gerhardus Muller    added userCmdReopenLog
# @version 1.7.0    22/10/2012    Gerhardus Muller    support to suppress the main loop waiting message in verbose mode
# @version 1.8.0    25/04/2013    Gerhardus Muller    socket error reporting in createTxProcSocket changed to use $!
# @version 1.9.0    13/05/2013    Gerhardus Muller    changed AppBase logging string for consistency
#
# perl -MCPAN -e "install IO::Handle, IO::Select, IO::Socket, Date::Manip::Date, Date::Manip::Delta"
#
package AppBase;
use strict;
use IO::Handle;
use IO::Select;
use IO::Socket;
use Date::Manip::Date;
use Date::Manip::Delta;
use Utils qw( hashToString listToString );
use Log qw( generateTimestamp closeLog $epochSecs $timestamp *LOGFILE );
use TxProc;

# constructor
sub new
{
  my ($class,$appName,$logDir,$bVerbose,$bFlushLogs,$buildStr) = @_;
#  print "$timestamp DEBUG AppBase::new 1\n";

  my $self = {
    appName             => $appName,
    logDir              => $logDir,
    curMonth            => 0,
    curDay              => 0,
    curHour             => 0,
    curMinute           => 0,
    bRunMonthly         => 0,
    bRunDaily           => 0,
    bRunHourly          => 0,
    bRunMinutely        => 0,
    bDateRunSkipZero    => 0,
    bCheckDateChanges   => 0,
    bLogOpen            => 0,
    bTimeToDie          => 0,
    bFrozen             => 1,
    ownQueue            => undef,
    workerPid           => -1,
    resultEvent         => undef,
    buildString         => $buildStr,
    beVerbose           => $bVerbose,
    bMainLoopVerbose    => $bVerbose,
    bFlushLogs          => $bFlushLogs
  };

  $self->{logFile} = "$logDir/$appName.log";
  $self->{emergencyLog} = "/tmp/$appName.log";

  bless $self, $class;
  return $self;
} # sub new

#######
# open log file - use stdout for interactive usage
sub openAppLog
{
  my ($this,$bUseStdErr,$bReopen) = @_;
  my $timestamp;

  $this->{logFile} = 'stderr' if($bUseStdErr);
  (*LOGFILE,$timestamp,$this->{bLogOpen}) = Log::openLog( $this->{logFile} );
  if( !$this->{bLogOpen} )
  {
    (*LOGFILE,$timestamp,$this->{bLogOpen}) = Log::openLog( $this->{emergencyLog} );
    die "ERROR failed to open the emergency log $this->{emergencyLog}" if(!$this->{bLogOpen});
    print LOGFILE "$timestamp ERROR AppBase::openAppLog: failed to write to main log:$this->{logFile}\n";
  } # if

  LOGFILE->autoflush(1) if($this->{bFlushLogs});
  print LOGFILE "\n$timestamp INFO AppBase::openAppLog: application started, frozen:$this->{bFrozen}\n" if(!$bReopen);
  print LOGFILE "$timestamp INFO AppBase::openAppLog: log reopened, frozen:$this->{bFrozen}\n" if($bReopen);
  print LOGFILE "$timestamp INFO AppBase::openAppLog: buildString:$this->{buildString}\n" if(defined($this->{buildString}));
} # sub openAppLog

#######
# user can override to do something useful
sub userCmdReopenLog
{
  my ($this) = @_;
} # userCmdReopenLog

#######
# create the io support
sub setupIoPoll
{
  my ($this) = @_;

  # stdout buffering wont work
  STDOUT->autoflush(1);           # required otherwise we do not see the buffered output when running in the persistent mode
  $this->{stdio} = new IO::Handle();
  $this->{stdio}->fdopen( fileno(STDIN),"r" );

  # create the select object and add the stdio handle
  $this->{selectObject} = new IO::Select();
  $this->{selectObject}->add( $this->{stdio} );
} # sub setupIoPoll

# add an additional filehandle for event polling
# @param $ioObj needs to be of type IO::Handle
sub addIoPollFh
{
  my ($this,$ioObj) = @_;
  $this->{selectObject}->add( $ioObj );
} # addIoPollFh

# removes a filehandle from event polling
# @param $ioObj needs to be of type IO::Handle
sub removeIoPollFh
{
  my ($this,$ioObj) = @_;
  $this->{selectObject}->remove( $ioObj );
} # removeIoPollFh

# creates a Unix Domain socket to txProc
# it is normally a requirement for this function to succeed
# @param $txProcUdSocket - the txProc side
# @param $txProcTcpAddr - can be undefined
# @param $txProcTcpService - can be undefined
sub createTxProcSocket
{
  my ($this,$txProcUdSocketPath,$txProcTcpAddr,$txProcTcpService) = @_;
  my $localName = "$this->{logDir}$this->{appName}.sock";
  $this->{txProcTcpAddr} = $txProcTcpAddr;
  $this->{txProcTcpService} = $txProcTcpService;
  unlink $localName;
  $this->{txProcSocket} = IO::Socket::UNIX->new( Peer=>$txProcUdSocketPath,Type=>SOCK_DGRAM );
  if( !defined($this->{txProcSocket}) )
  {
    print LOGFILE "$timestamp WARN AppBase::createTxProcSocket failed on localName:$localName peer:$txProcUdSocketPath - '$!'\n";
    return 0;
  } # if
  else
  {
    my $bindRes = $this->{txProcSocket}->bind( pack_sockaddr_un($localName) );
    $bindRes = "failed:$1" if(!defined($bindRes));
    $txProcTcpAddr = '' if(!defined($txProcTcpAddr));
    $txProcTcpService = '' if(!defined($txProcTcpService));
    print LOGFILE "$timestamp INFO AppBase::createTxProcSocket localName:$localName peer:$txProcUdSocketPath fallback TCP addr:$txProcTcpAddr service:$txProcTcpService bindRes:$bindRes\n";
    return 1;
  } # else
} # createTxProcSocket

# main loop
sub run
{
  my ($this) = @_;

  while( !$this->{bTimeToDie} )
  {
    $timestamp = generateTimestamp();
    $this->checkDateChanges() if( $this->{bCheckDateChanges} );
    $this->startLoopProcess();

    print LOGFILE "$timestamp INFO AppBase::run waiting for new packet/event frozen:$this->{bFrozen}\n" if($this->{bMainLoopVerbose});

    my @ready;
    my @extraEvents;
    @ready=$this->{selectObject}->can_read();

    # execute if there are any events ready
    if( @ready > 0 )
    {
      $timestamp = generateTimestamp();
      foreach my $fh (@ready)
      {
        if( $fh == $this->{stdio} )
        {
          if( $this->{stdio}->eof() )
          {
            print LOGFILE "$timestamp INFO AppBase::run stdio eof\n";
            $this->{bTimeToDie} = 1;
          } # if
          else
          {
            my $outputString;
            eval
            {
              my ($txProcEvent,$err) = TxProc->unSerialise( $this->{stdio} );
              if( $txProcEvent )
              {
                $this->constructDefaultResultEvent( $txProcEvent );
                my $newEvents = $this->handleNewEvent( $txProcEvent );
                if( defined($newEvents) )
                {
                  if( ref($newEvents) eq 'ARRAY' )
                  {
                    push @extraEvents,@$newEvents if(@$newEvents>0);
                  } # if ARRAY
                  else
                  {
                    print LOGFILE "$timestamp WARN AppBase::run expects handleNewEvent to return an array reference or undef - not a '".ref($newEvents)."' newEvents:$newEvents\n";
                  } # else
                } # if defined newEvents

                # write a mandatory response
                $outputString = $this->sendDone();
              } # if
              else
              {
                print LOGFILE "$timestamp WARN AppBase::run($this->{appName}) failed to successfully deserialise txProc object err:$err\n";
                $this->{resultEvent} = new TxProc( 'EV_RESULT' ) if(!defined($this->{resultEvent}));
                $this->{resultEvent}->bSuccess(0);
                $this->{resultEvent}->errorString( "AppBase::run($this->{appName}) failed to successfully deserialise txProc object err:$err" );
                $outputString = $this->sendDone();
              } # else
            }; # eval
            if( $@ )
            {
              print LOGFILE "$timestamp WARN AppBase::run($this->{appName}) application exception: '$@'\n";
              $this->{resultEvent} = new TxProc( 'EV_RESULT' ) if(!defined($this->{resultEvent}));
              $this->{resultEvent}->bSuccess(0);
              $this->{resultEvent}->errorString( " AppBase::run($this->{appName}) application exception: '$@'" );
              $outputString = $this->sendDone();
            } # if

            # write a mandatory response
            print $outputString;
          } # else !stdio eof
        } # if $fh == $stdio
        else
        {
          eval
          {
            my $newEvents = $this->handlePolledFh( $fh );
            if( defined($newEvents) )
            {
              if( ref($newEvents) eq 'ARRAY' )
              {
                push @extraEvents,@$newEvents if(@$newEvents>0);
              } # if ARRAY
              else
              {
                print LOGFILE "$timestamp WARN AppBase::run expects handlePolledFh to return an array reference or undef\n";
              } # else
            } # if defined newEvents
          }; # eval
          print LOGFILE "$timestamp ERROR AppBase::run handlePolledFh exception:'$@'\n" if($@);
        } # else
      } # foreach $fh
    } # if @ready

    # hook for maintenance events for which a timer runs that breaks the wait - we provide the hook here so 
    # that any events it produces are immediately dispatched
    my $newEvents;
    $newEvents = $this->execRegularTasks() if( !$this->{bFrozen} );
    if( defined($newEvents) )
    {
      if( ref($newEvents) eq 'ARRAY' )
      {
        push @extraEvents,@$newEvents if(@$newEvents>0);
      } # if ARRAY
      else
      {
        print LOGFILE "$timestamp WARN AppBase::run expects execRegularTasks to return an array reference or undef - not a '".ref($newEvents)."' newEvents:$newEvents\n";
      } # else
    } # if defined newEvents

    # submit events generated by event handling functions
    # handle EV_BASE events internally by calling handleNewEvent directly
    # submit the rest directly to txProc
    eval
    {
      if( @extraEvents > 0 )
      {
        my $event;
        while( ($event = shift @extraEvents) )
        {
          if( $event->eventType() eq 'EV_BASE' )
          {
            my $newEvents = $this->handleNewEvent( $event );
            if( defined($newEvents) )
            {
              if( ref($newEvents) eq 'ARRAY' )
              {
                push @extraEvents,@$newEvents if(@$newEvents>0);
              } # if ARRAY
              else
              {
                print LOGFILE "$timestamp WARN AppBase::run expects handleNewEvent 1 to return an array reference or undef\n";
              } # else
            } # if defined newEvents
          } # if EV_BASE
          else
          {
            my ($retVal,$errString) = $event->serialise( $this->eventSerialiseParameters() );
            if( !$retVal )
            {
              print LOGFILE "$timestamp ERROR AppBase::run failed to submit to txProc:'$errString' event:".$event->toString()."\n";
            } # if
            else
            {
              print LOGFILE "$timestamp INFO AppBase::run submitted to txProc:".$event->toString()."\n" if($this->{beVerbose});
            } # else
          } # else EV_BASE
        } # while shift extraEvents
      } # if @extraEvents > 0
    }; # eval
    print LOGFILE "$timestamp ERROR AppBase::run extra event application exception: '$@'\n "if( $@ );
  } # while( !$bTimeToDie
} # sub run

######
######
# callbacks that can be overridden a the derived class
######

######
# called at the start of the loop before waiting for any new events
# always runs
sub startLoopProcess
{
  my ($this) = @_;
} # sub startLoopProcess

######
# called after startLoopProcess but only if not frozen
sub execRegularTasks
{
  my ($this) = @_;
} # sub execRegularTasks

######
# called if a file handle other than stdio is returned by the poll function
# @param $fh - contains the IO::Handle that was passed to addIoPollFh
# @return undef or a reference to an array of new events to be processed 
sub handlePolledFh
{
  my ($this,$fh) = @_;
  $this->removeIoPollFh( $fh );
  print LOGFILE "$timestamp ERROR AppBase::handlePolledFh unrecognised file handle:$fh - removed from poll object\n";
  return;
} # sub handlePolledFh

######
# handle new application events received from txProc - do not normally override - rather implement ..
# @param $event
# @return undef or a reference to an array of new events to be processed 
sub handleNewEvent
{
  my ($this,$event) = @_;
  my $newEvents;

  print LOGFILE "$timestamp INFO AppBase::handleNewEvent:'".$event->toString."'\n" if($this->{beVerbose});
  if( $event->eventType() eq 'EV_COMMAND' )
  {
    $newEvents = $this->handleEvCommand( $event );
  } # if EV_COMMAND
  elsif( $event->eventType() eq 'EV_PERL' )
  {
    $newEvents = $this->handleEvPerl( $event );
  } # elsif EV_PERL
  elsif( $event->eventType() eq 'EV_URL' )
  {
    $newEvents = $this->handleEvUrl( $event );
  } # elsif EV_URL
  elsif( $event->eventType() eq 'EV_RESULT' )
  {
    $newEvents = $this->handleEvResult( $event );
  } # elsif EV_RESULT
  elsif( $event->eventType() eq 'EV_BASE' )
  {
    $newEvents = $this->handleEvBase( $event );
  } # elsif EV_BASE
  elsif( $event->eventType() eq 'EV_ERROR' )
  {
    $newEvents = $this->handleEvError( $event );
  } # elsif EV_BASE
  else
  {
    $newEvents = $this->handleEvOther( $event );
  } # else

  return $newEvents;
} # sub handleNewEvent

######
# handles EV_COMMAND events. does not normally need to be overridden
# it invokes handlePersistentCommand,handleUnhandledCmdEvents and indirectly handleUserPersistentCommand
# that would be more appropriate ot re-implement
# @param $event
sub handleEvCommand
{
  my ($this,$event) = @_;
  my $newEvents;

  my $command = $event->command();
  if( $command eq 'CMD_REOPEN_LOG' )
  {
    # does not make sense to invoke this if we are running against stderr
    closeLog();
    $this->openAppLog(0,1);
    $this->userCmdReopenLog();
  } # elsif CMD_REOPEN_LOG
  elsif( $command eq 'CMD_STATS' )
  {
    $newEvents = $this->generateStats( $event );
  } # elsif CMD_STATS
  elsif( $command eq 'CMD_EXIT_WHEN_DONE' )
  {
    $this->{bTimeToDie} = 1;
    $newEvents = $this->prepareToExit( $event );
  } # elsif CMD_EXIT_WHEN_DONE
  elsif( $command eq 'CMD_PERSISTENT_APP' )
  {
    $newEvents = $this->handlePersistentCommand( $event );
  } # elsif CMD_PERSISTENT_APP
  else
  {
    $newEvents = $this->handleUnhandledCmdEvents( $event );
  } # else

  return $newEvents;
} # sub handleEvCommand

#######
# called for unhandled CMD_ events
sub handleUnhandledCmdEvents
{
  my ($this,$event) = @_;
  my $newEvents;
  print LOGFILE "$timestamp INFO AppBase::handleUnhandledCmdEvents: unable to handle command:".$event->command()."\n";

  return $newEvents;
} # sub handleUnhandledCmdEvents

#######
# handles persistent app command events - typically controlling/configuring the app
# implement for customised behaviour - default behaviour can freeze / unfreeze and exit - typically to upgrade code or settings
sub handlePersistentCommand
{
  my ($this,$event) = @_;
  my $newEvents;

  my $cmd = $event->getParam( 'cmd' );
  if( !defined($cmd) )
  {
    $newEvents = $this->handleUserPersistentCommand( $event );
  } # if
  elsif( $cmd eq "stop" )
  {
    $this->{bFrozen} = 1;
    print LOGFILE "$timestamp INFO AppBase::handlePersistentCommand: freezing execution\n";
  } # elsif stop
  elsif( $cmd eq "start" )
  {
    $this->{bFrozen} = 0;
    print LOGFILE "$timestamp INFO AppBase::handlePersistentCommand: unfreezing execution\n";
  } # if start
  elsif( $cmd eq "exit" )
  {
    $this->{bTimeToDie} = 1;
    $newEvents = $this->prepareToExit( $event );
    print LOGFILE "$timestamp INFO AppBase::handlePersistentCommand: exiting\n";
  } # if exit
  elsif( $cmd eq "startupinfo" )
  {
    $this->{ownQueue} = $event->getParam('ownqueue');
    $this->{workerPid} = $event->getParam('workerpid');
    print LOGFILE "$timestamp INFO AppBase::handlePersistentCommand: ownQueue:$this->{ownQueue} workerPid:$this->{workerPid}\n";
  } # if exit
  else
  {
    $newEvents = $this->handleUserPersistentCommand( $event );
  } # else

  return $newEvents;
} # handlePersistentCommand

######
# handles user extensions to persistent commands
# should be implemented to do something useful
# the extensions are either additional 'cmd' values or events not using the 'cmd' syntax
sub handleUserPersistentCommand
{
  my ($this,$event) = @_;
  my $newEvents = [];

  my $cmd = $event->getParam( 'cmd' );
  if( defined($cmd) )
  {
    my $str =  "cmd:'$cmd' not supported";
    print LOGFILE "$timestamp WARN AppBase::handleUserPersistentCommand: $str for:'".$event->toString()."'\n";
    push @$newEvents, $this->createReturnEvent($event,undef,'failed',$str);
  } # if defined $cmd
  else
  {
    print LOGFILE "$timestamp WARN AppBase::handleUserPersistentCommand: no parameter 'cmd' for:'".$event->toString()."'\n";
    push @$newEvents, $this->createReturnEvent($event,undef,'failed','no cmd');
  } # else

  return $newEvents;
} # sub handleUserPersistentCommand

######
# handles EV_PERL events. implement to do something useful
# @param $event
sub handleEvPerl
{
  my ($this,$event) = @_;
  my $newEvents = [];

  print LOGFILE "$timestamp WARN AppBase::handleEvPerl: cannot handle event:'".$event->toString()."'\n";
  push @$newEvents, $this->createReturnEvent($event,undef,'failed','no handler');

  return $newEvents;
} # sub handleEvPerl

######
# handles EV_URL events. implement to do something useful
# @param $event
sub handleEvUrl
{
  my ($this,$event) = @_;
  my $newEvents = [];

  print LOGFILE "$timestamp WARN AppBase::handleEvUrl: cannot handle event:'".$event->toString()."'\n";
  push @$newEvents, $this->createReturnEvent($event,undef,'failed','no handler');

  return $newEvents;
} # sub handleEvUrl

######
# handles EV_RESULT events. implement to do something useful
# @param $event
sub handleEvResult
{
  my ($this,$event) = @_;
  my $newEvents = [];

  # EV_RESULT events are normally in response to a service request - cannot send a return event
  print LOGFILE "$timestamp WARN AppBase::handleEvResult: cannot handle event:'".$event->toString()."'\n";

  return $newEvents;
} # sub handleEvResult

######
# handles EV_BASE events - these are normally internal to the application. implement to do something useful
# @param $event
sub handleEvBase
{
  my ($this,$event) = @_;
  my $newEvents;

  # EV_BASE are application internal - cannot send a return event
  print LOGFILE "$timestamp WARN AppBase::handleEvBase: cannot handle event:'".$event->toString()."'\n";

  return $newEvents;
} # sub handleEvBase

######
# handles EV_ERROR events - these are produced by txProc on failure to handle an event. implement to do something useful
# @param $event
sub handleEvError
{
  my ($this,$event) = @_;
  my $newEvents;

  # EV_ERROR are already error events - cannot send a return event
  print LOGFILE "$timestamp WARN AppBase::handleEvError: cannot handle event:'".$event->toString()."'\n";

  return $newEvents;
} # sub handleEvError

######
# handles events other than EV_COMMAND,EV_PERL,EV_RESULT. implement to do something useful
# @param $event
sub handleEvOther
{
  my ($this,$event) = @_;
  my $newEvents = [];

  print LOGFILE "$timestamp WARN AppBase::handleEvOther: cannot handle event:'".$event->toString()."'\n";
  push @$newEvents, $this->createReturnEvent($event,undef,'failed','no handler');

  return $newEvents;
} # sub handleEvOther

#######
sub constructDefaultResultEvent
{
  my ($this,$event) = @_;
  $this->{resultEvent} = new TxProc( 'EV_RESULT' );
  $this->{resultEvent}->bSuccess(1);
  $this->{resultEvent}->reference( $event->reference() );
  my $resultQueue = $event->getParam('resultQueue');
  $this->{resultEvent}->destQueue( $resultQueue ) if(defined($resultQueue));
  $this->{resultEvent}->addParam( 'generatedby', $this->{appName} );
  print LOGFILE "$timestamp DEBUG constructDefaultResultEvent: ref event:".ref($event)."\n";
} # constructDefaultResultEvent

#######
# constructs a mandatory response
sub sendDone
{
  my ($this) = @_;
  if( !defined($this->{resultEvent}) )
  {
    print LOGFILE "$timestamp DEBUG sendDone: creating resultEvent\n";
    $this->{resultEvent} = new TxProc( 'EV_RESULT' );
    $this->{resultEvent}->bSuccess(1);
  } # if
  my $resultString = $this->{resultEvent}->serialiseToString();
  undef($this->{resultEvent});
  return $resultString;
} # sendDone

#######
# should be overridden to do something useful
sub generateStats
{
  my ($this,$event) = @_;
  my $newEvents;

  return $newEvents;
} # sub generateStats

#######
# called on receiving CMD_EXIT_WHEN_DONE or a cmd:exit
sub prepareToExit
{
  my ($this,$event) = @_;
  my $newEvents;

  return $newEvents;
} # sub prepareToExit

######
# utility functions
######

######
# creates a return EV_RESULT TxProc event
sub createReturnEvent
{
  my ($this,$event,$type,$status,$error) = @_;

  my $eventL = new TxProc( 'EV_RESULT' );
  $eventL->destQueue( $event->getParam('resultQueue') );
  $eventL->result( $status );
  $eventL->reference( $event->reference() );
  $eventL->addParam( 'type', $type ) if( defined($type) );
  $eventL->addParam( 'error', $error ) if( defined($error) );
  $eventL->addParam( 'generatedby', $this->{appName} );

  return $eventL;
} # createReturnEvent

######
sub checkDateChanges
{
  my ($this) = @_;
  $this->{date} = new Date::Manip::Date if(!exists($this->{date})); 
  $this->{date}->secs_since_1970_GMT( $epochSecs );
  $this->{dateStringNow} = $this->{date}->printf('%Y-%m-%d %H:%M:%S');
  my @vals = $this->{date}->value();    # 2010,11,16,20,10,5
  my $newMonth = $vals[1];
  my $newDay = $vals[2];
  my $newHour = $vals[3];
  my $newMinute = $vals[4];

  if( $newMinute != $this->{curMinute} )
  {
    print LOGFILE "$timestamp INFO checkDateChanges: curMinute:$this->{curMinute} newMinute:$newMinute\n";
    $this->{curMinute} = $newMinute;
    $this->{bRunMinutely} = 1 if( !$this->{bDateRunSkipZero} || ($this->{curMinute} != 0) );
  } # if
  if( $newHour != $this->{curHour} )
  {
    print LOGFILE "$timestamp INFO checkDateChanges: curHour:$this->{curHour} newHour:$newHour\n";
    $this->{curHour} = $newHour;
    $this->{bRunHourly} = 1 if( !$this->{bDateRunSkipZero} || ($this->{curHour} != 0) );
  } # if
  if( $newDay != $this->{curDay} )
  {
    print LOGFILE "$timestamp INFO checkDateChanges: curDay:$this->{curDay} newDay:$newDay\n";
    $this->{curDay} = $newDay;
    $this->{bRunDaily} = 1 if( !$this->{bDateRunSkipZero} || ($this->{curDay} != 0) );
  } # if
  if( $newMonth != $this->{curMonth} )
  {
    print LOGFILE "$timestamp INFO checkDateChanges: curMonth:$this->{curMonth} newMonth:$newMonth\n";
    $this->{curMonth} = $newMonth;
    $this->{bRunMonthly} = 1;
  } # if
} # sub checkDateChanges

######
# data member access functions
######
sub appName
{
  my ($this,$val) = @_;
  $this->{appName} = $val if(defined($val));
  return $this->{appName};
} # sub appName
sub logDir
{
  my ($this,$val) = @_;
  $this->{logDir} = $val if(defined($val));
  return $this->{logDir};
} # sub logDir
sub curMonth
{
  my ($this,$val) = @_;
  $this->{curMonth} = $val if(defined($val));
  return $this->{curMonth};
} # sub curMonth
sub curDay
{
  my ($this,$val) = @_;
  $this->{curDay} = $val if(defined($val));
  return $this->{curDay};
} # sub curDay
sub curHour
{
  my ($this,$val) = @_;
  $this->{curHour} = $val if(defined($val));
  return $this->{curHour};
} # sub curHour
sub curMinute
{
  my ($this,$val) = @_;
  $this->{curMinute} = $val if(defined($val));
  return $this->{curMinute};
} # sub curMinute
sub bRunMonthly
{
  my ($this,$val) = @_;
  $this->{bRunMonthly} = $val if(defined($val));
  return $this->{bRunMonthly};
} # sub bRunMonthly
sub bRunDaily
{
  my ($this,$val) = @_;
  $this->{bRunDaily} = $val if(defined($val));
  return $this->{bRunDaily};
} # sub bRunDaily
sub bRunHourly
{
  my ($this,$val) = @_;
  $this->{bRunHourly} = $val if(defined($val));
  return $this->{bRunHourly};
} # sub bRunHourly
sub bRunMinutely
{
  my ($this,$val) = @_;
  $this->{bRunMinutely} = $val if(defined($val));
  return $this->{bRunMinutely};
} # sub bRunMinutely
sub bDateRunSkipZero
{
  my ($this,$val) = @_;
  $this->{bDateRunSkipZero} = $val if(defined($val));
  return $this->{bDateRunSkipZero};
} # sub bDateRunSkipZero
sub bCheckDateChanges
{
  my ($this,$val) = @_;
  $this->{bCheckDateChanges} = $val if(defined($val));
  return $this->{bCheckDateChanges};
} # sub bCheckDateChanges
sub bLogOpen
{
  my ($this,$val) = @_;
  $this->{bLogOpen} = $val if(defined($val));
  return $this->{bLogOpen};
} # sub bLogOpen
sub bTimeToDie
{
  my ($this,$val) = @_;
  $this->{bTimeToDie} = $val if(defined($val));
  return $this->{bTimeToDie};
} # sub bTimeToDie
sub bFrozen
{
  my ($this,$val) = @_;
  $this->{bFrozen} = $val if(defined($val));
  return $this->{bFrozen};
} # sub bFrozen
sub beVerbose
{
  my ($this,$val) = @_;
  $this->{beVerbose} = $val if(defined($val));
  return $this->{beVerbose};
} # sub beVerbose
# can be used directly as the argument to $event->serialise()
sub eventSerialiseParameters
{
  my ($this) = @_;
#  print LOGFILE "$timestamp DEBUG eventSerialiseParameters: txProcSocket:$this->{txProcSocket} txProcTcpAddr:$this->{txProcTcpAddr} txProcTcpService:$this->{txProcTcpService}\n";
  return ($this->{txProcSocket},undef,$this->{txProcTcpAddr},$this->{txProcTcpService});
} # sub 
#sub 
#{
#  my ($this,$val) = @_;
#  $this->{} = $val if(defined($val));
#  return $this->{};
#} # sub 
#sub 
#{
#  my ($this,$val) = @_;
#  $this->{} = $val if(defined($val));
#  return $this->{};
#} # sub 

1;  # module return value
