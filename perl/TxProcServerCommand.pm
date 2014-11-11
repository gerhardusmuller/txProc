# Generic command object for txProc
#
# $Id: TxProcServerCommand.pm 3055 2014-03-26 12:32:50Z gerhardus $
# Gerhardus Muller
# Versioning: a.b.c a is a major release, b represents changes or new features, c represents bug fixes. 
# @version 1.0.0		25/11/2009		Gerhardus Muller		script created
# @version 1.0.1		25/03/2014		Gerhardus Muller		val parameter to createqueues has to be a string
#
package TxProcServerCommand;
use strict;
use lib '/home/vts/vts/perl/modules';
use TxProc;

# constructor
# set connection parameters to undef if not to be used
# Unix Domain socket connections take preference ie if specified
# the TCP parameters won't be used
# if the serverName is specified to be '-' the serialised object is written to stdout
sub new
{
  my ($class,$serverName,$serverService,$unixPath) = @_;

  my $self = {
    serverName =>     $serverName,
    service =>        $serverService,
    udSocket =>       $unixPath
  };

  bless $self, $class;
  return $self;
} # sub new

######
# sends a CMD_REOPEN_LOG to the server
sub reopenLog
{
  my ($this) = @_;
  return $this->sendCommand( 'CMD_REOPEN_LOG', undef );
} # reopenLog

######
# sends a CMD_SHUTDOWN to the server
sub shutdown
{
  my ($this) = @_;
  return $this->sendCommand( 'CMD_SHUTDOWN', undef );
} # shutdown

######
# sends a CMD_STATS to the server
sub stats
{
  my ($this) = @_;
  return $this->sendCommand( 'CMD_STATS', undef );
} # stats

######
# sends a CMD_DUMP_STATE to the server
sub dumpstate
{
  my ($this,$filename) = @_;
  my $cmdParams;
  $cmdParams = {filename=>$filename} if( defined($filename) );
  return $this->sendCommand( 'CMD_DUMP_STATE', $cmdParams );
} # dumpstate

######
#
sub setMainLogLevel
{
  my ($this,$val) = @_;
  my $cmdParams = {cmd=>'loglevel', val=>$val};
  return $this->mainConfCmd( $cmdParams );
} # setMainLogLevel

######
#
sub setNucleusLogLevel
{
  my ($this,$val) = @_;
  my $cmdParams = {cmd=>'loglevel', val=>$val};
  return $this->nucleusConfCmd( $cmdParams );
} # setNucleusLogLevel

######
#
sub setNetworkIfLogLevel
{
  my ($this,$val) = @_;
  my $cmdParams = {cmd=>'loglevel', val=>$val};
  return $this->networkIfConfCmd( $cmdParams );
} # setNetworkIfLogLevel

######
#
sub setMaxQueueLen
{
  my ($this,$queue,$val) = @_;
  my $cmdParams = {cmd=>'updatemaxqueuelen', queue=>"$queue", val=>$val};
  return $this->nucleusConfCmd( $cmdParams );
} # setMaxQueueLen

######
#
sub setMaxExecTime
{
  my ($this,$queue,$val) = @_;
  my $cmdParams = {cmd=>'updatemaxexectime', queue=>"$queue", val=>$val};
  return $this->nucleusConfCmd( $cmdParams );
} # setMaxExecTime

######
#
sub setNumWorkers
{
  my ($this,$queue,$val) = @_;
  my $cmdParams = {cmd=>'updateworkers', queue=>"$queue", val=>$val};
  return $this->nucleusConfCmd( $cmdParams );
} # setNumWorkers

######
#
sub freeze
{
  my ($this,$queue,$val) = @_;
  my $cmdParams = {cmd=>'freeze', queue=>"$queue", val=>$val};
  return $this->nucleusConfCmd( $cmdParams );
} # freeze

######
#
sub createQueue
{
  my ($this,$queue,$val) = @_;
  my $cmdParams = {cmd=>'createqueue', queue=>"$queue", val=>"$val"};
  return $this->nucleusConfCmd( $cmdParams );
} # 

######
# sends a CMD_NUCLEUS_CONF to the server
# the params should typically contain 'cmd' and likely 'queue', often 'val'
sub nucleusConfCmd
{
  my ($this,$params) = @_;
  return $this->sendCommand( 'CMD_NUCLEUS_CONF', $params );
} # nucleusConfCmd

######
# sends a CMD_NETWORKIF_CONF to the server
# the params should typically contain 'cmd' and other parameters
sub networkIfConfCmd
{
  my ($this,$params) = @_;
  return $this->sendCommand( 'CMD_NETWORKIF_CONF', $params );
} # nucleusConfCmd

######
# sends a CMD_MAIN_CONF to the server
# the params should typically contain 'cmd' and other parameters
sub mainConfCmd
{
  my ($this,$params) = @_;
  return $this->sendCommand( 'CMD_MAIN_CONF', $params );
} # mainConfCmd

######
# sends a generic command to the server
sub sendCommand
{
  my ($this,$command,$params) = @_;
  my $event = new TxProc();
  $event->eventType( 'EV_COMMAND' );
  $event->command( $command );
  $event->execParams( $params ) if( defined($params) );
  my ($retVal,$errString) = $event->serialise( undef,$this->{udSocket},$this->{serverName},$this->{service} );

  if( !$retVal )
  {
    print STDERR "ServerCommand::sendCommand ERROR failed to submit to txProc:'$errString'\n";
    return (0,$errString);
  } # if
  return (1,undef);
} # sendCommand

1;
