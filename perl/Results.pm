# results logic class
#
# $Id$
# Gerhardus Muller
#
# Versioning: a.b.c a is a major release, b represents changes or new features, c represents bug fixes. 
# @version 1.0.0		05/03/2012		Gerhardus Muller		Script created
# @version 1.1.0		11/12/2012		Gerhardus Muller		made resDescriptor a property and added getResDescriptor
#
# perl -MCPAN -e "install JSON::XS"
#
# Copyright Gerhardus Muller
#
package Results;
use strict;
use Utils qw( hashToString referenceToString );
use Log qw( generateTimestamp openLog closeLog $epochSecs $timestamp *LOGFILE );
use TxProc;
use AppBase;
use ConfigResults qw();
use JSON::XS;
use IO::Handle;
use IO::Select;
use IO::Socket;
use English;

# globals / constants
our $json = JSON::XS->new->allow_nonref;

# constructor
sub new
{
  my ($class,$bVerbose) = @_;

  my $self = {
    beVerbose           => $bVerbose,
    socket              => undef,
    type                => undef,
    resDescriptor       => undef,
    hostname            => undef,
  };
  
  bless $self, $class;
  return $self;
} # sub new

######dispatchResult
sub dispatchResult
{
  my ($this,$event,$jDesc) = @_;
  my $descriptor;

  eval
  {
    $descriptor = $json->decode( $jDesc );
  };
  if( $@ )
  {
    print LOGFILE "$timestamp WARN dispatchResult: jDesc:'$jDesc' from_json barfed: '$@'\n";
    return;
  } # if

  my $type = $descriptor->{type};
  if( $type eq 'localDgram' )
  {
    print LOGFILE "$timestamp INFO dispatchResult type:$type path:$descriptor->{path}\n" if($this->{beVerbose});
    my $unixdomainPath = $descriptor->{path};
    my $socket = IO::Socket::UNIX->new(  
      Peer    => $unixdomainPath,
      Type    => SOCK_DGRAM
    );
    if( !defined($socket) )
    {
      print LOGFILE "$timestamp WARN dispatchResult localDgram: IO::Socket::UNIX->new failed - path:'$unixdomainPath' - $@\n";
      return;
    } # if
    my ($retVal,$errorString) = $event->serialise( $socket, undef, undef, undef );
    close $socket;
    if( !$retVal )
    {
      print LOGFILE "$timestamp WARN dispatchResult localDgram: serialise failed - $errorString\n";
      return;
    } # if
  } # if
  elsif( $type eq 'localStream' )
  {
    print LOGFILE "$timestamp INFO dispatchResult type:$type path:$descriptor->{path}\n" if($this->{beVerbose});
    my $unixdomainPath = $descriptor->{path};
    my $socket = IO::Socket::UNIX->new(  
      Peer    => $unixdomainPath,
      Type    => SOCK_STREAM
    );
    if( !defined($socket) )
    {
      print LOGFILE "$timestamp WARN dispatchResult localStream: IO::Socket::UNIX->new failed - path:'$unixdomainPath' - $@\n";
      return;
    } # if
    my ($retVal,$errorString) = $event->serialise( $socket, undef, undef, undef );
    close $socket;
    if( !$retVal )
    {
      print LOGFILE "$timestamp WARN dispatchResult localStream: serialise failed - $errorString\n";
      return;
    } # if
  } # if
  elsif( $type eq 'tcpStream' )
  {
    print LOGFILE "$timestamp INFO dispatchResult type:$type address:$descriptor->{serverName} service:$descriptor->{serverService}\n" if($this->{beVerbose});
    my $serverName = $descriptor->{serverName};
    my $serverService = $descriptor->{serverService};
    my $socket = IO::Socket::INET->new( 
      PeerAddr  => $serverName,
      PeerPort  => $serverService,
      Proto     => 'tcp',
      Type      => SOCK_STREAM
    );
    if( !defined($socket) )
    {
      print LOGFILE "$timestamp WARN dispatchResult tcpStream: IO::Socket::INET->new failed - server:$serverName, service:$serverService - $@\n";
      return;
    } # if
    my ($retVal,$errorString) = $event->serialise( $socket, undef, undef, undef );
    close $socket;
    if( !$retVal )
    {
      print LOGFILE "$timestamp WARN dispatchResult tcpStream: serialise failed - $errorString\n";
      return;
    } # if
  } # if
  else
  {
    print LOGFILE "$timestamp WARN dispatchResult: unknown type:'$type'\n";
  } # else
} # sub dispatchResult

######writeGreeting
# txProc writes a greeting on a stream socket on connect
sub writeGreeting
{
  my ($this,$socket) = @_;
  if( !defined($this->{hostname}) )
  {
    $this->{hostname} = `hostname`;
    chomp $this->{hostname};
  } # if
  my $greetingString = printf( "%s@%s pver %s md %d", 'Results', $this->{hostname}, '3.0', 32768 );
  my $greeting = printf( "%03d:%s", length($greetingString), $greetingString );
  my $retVal = $socket->send( $greeting );
  return (0,"writeGreeting failed - $!") if(!$retVal);
  return (1,undef);
} # writeGreeting

######printResultToSocket
# txProc writes a success/fail on a stream socket after reading a packet
sub printResultToSocket
{
  my ($this,$socket,$ref,$bSuccess) = @_;
  my $event = new TxProc( 'EV_REPLY' );
  $event->reference( $ref );
  $event->bSuccess( $bSuccess );
  $event->bExpectReply( 0 );
  my ($retVal,$errorString) = $event->serialise( $socket, undef, undef, undef );
  return ($retVal,$errorString);
} # printResultToSocket

######createResultSocket
# creates a socket which can be waited on for a result
# @param $type - 'localDgram','localStream','tcpStream'
# @param $address - IP address to listen on or Unix Domain path - undef for INADDR_ANY (IP only)
# @param $service - IP port to listen on otherwise undef for a random port if applicable
sub createResultSocket
{
  my ($this,$type,$address,$service) = @_;
  $this->{resDescriptor} = {};

  $this->{type} = $type;
  $this->{resDescriptor}->{type} = $type;

  if( $type eq 'localDgram' )
  {
    # prep the unix domain socket name
    umask(0); # we actually want a mask of 7 and uucp a member of the www group but writing failes
    $address .= '/' if(substr($address,-1) ne '/');
    my $path = $address."result".$PID.".sock";
    $this->{path} = $path;
    $this->{resDescriptor}->{path} = $path;
    unlink $path;
    $this->{socket} = IO::Socket::UNIX->new(  
      Local   => $path,
      Type    => SOCK_DGRAM,
      Listen  => SOMAXCONN
    );
    return (undef, "createResultSocket IO::Socket::UNIX->new failed - path:'$path' - $@" ) if(!defined($this->{socket}));
    print LOGFILE "$timestamp INFO createResultSocket type:$type path:$path\n" if($this->{beVerbose});
    return ($this->{resDescriptor},undef);
  } # if
  elsif( $type eq 'localStream' )
  {
    # prep the unix domain socket name
    umask(07);
    $address .= '/' if(substr($address,-1) ne '/');
    my $path = $address."result".$PID.".sock";
    $this->{path} = $path;
    $this->{resDescriptor}->{path} = $path;
    unlink $path;
    $this->{socket} = IO::Socket::UNIX->new(  
      Local   => $path,
      Type    => SOCK_STREAM,
      Listen  => SOMAXCONN
    );
    return (undef, "createResultSocket IO::Socket::UNIX->new failed - path:'$path' - $@" ) if(!defined($this->{socket}));
    print LOGFILE "$timestamp INFO createResultSocket type:$type path:$path\n" if($this->{beVerbose});
    return ($this->{resDescriptor},undef);
  } # elsif
  elsif( $type eq 'tcpStream' )
  {
    # TODO - LocalPort should possibly not be part of the request if $service is not define - investigate
    $this->{socket} = IO::Socket::INET->new( 
      LocalHost => $address,
      LocalPort => $service,
      Proto     => 'tcp',
      Type      => SOCK_STREAM,
      Listen    => SOMAXCONN,
      ReuseAddr => 1,
      ReusePort => 1
    );
    return (undef, "createResultSocket IO::Socket::INET->new failed - server:$address, service:$service - $@" ) if(!defined($this->{socket}));
    $this->{resDescriptor}->{serverName} = $this->{socket}->sockhost();
    $this->{resDescriptor}->{serverService} = $this->{socket}->sockport();
    print LOGFILE "$timestamp INFO createResultSocket type:$type address:$this->{resDescriptor}->{serverName} service:$this->{resDescriptor}->{serverService}\n" if($this->{beVerbose});
    return ($this->{resDescriptor},undef);
  } # elsif
  else
  {
    return (undef,"createResultSocket: unknown type:'$type'");
  } # else
} # createResultSocket

######getResDescriptor
sub getResDescriptor
{
  my ($this) = @_;
  return $this->{resDescriptor};
} # getResDescriptor

######closeResultSocket
sub closeResultSocket
{
  my ($this) = @_;
  return if(!defined($this->{socket}));

  if( ($this->{type} eq 'localDgram') || ($this->{type} eq 'localStream') )
  {
    close $this->{socket};
    unlink $this->{path};
  } # if
  elsif( $this->{type} eq 'tcpStream' )
  {
    close $this->{socket};
  } # elsif
  print LOGFILE "$timestamp INFO closeResultSocket type:$this->{type} path:$this->{path}\n";
} # closeResultSocket

######waitForResults
# Note - is both accept and waitForData system restartable?
# @param $timeout - can be undef or 0 for infinite otherwise in seconds
sub waitForResults
{
  my ($this,$timeout) = @_;
  return (undef,'no socket') if(!defined($this->{socket}));
  my $readSocket;

  eval
  {
    # limit the time we wait
    local $SIG{ALRM} = sub { die "alarm\n" };

    if( $this->{type} eq 'localDgram' )
    {
      $readSocket = $this->{socket};
    } # if
    else
    {
      print LOGFILE "$timestamp INFO waitForResults entering accept\n" if($this->{beVerbose});
      alarm( $timeout ) if(defined($timeout));
      $readSocket = $this->{socket}->accept() or die("cannot accept connection: $!\n");
      alarm(0) if(defined($timeout));
      my ($retVal,$errorString) = $this->writeGreeting( $readSocket );
      if( !$retVal )
      {
        print LOGFILE "$timestamp WARN waitForResults stream: writeGreeting failed - $errorString\n";
        return (undef,"error writing greeting - $errorString");
      } # if
    } # else
  }; # eval
  if( $@ )
  {
    die unless $@ eq "alarm\n";
    print LOGFILE "$timestamp INFO waitForResults: accept aborted waiting\n";
    return (undef,'aborted waiting');
  } # if

  print LOGFILE "$timestamp INFO waitForResults waiting for data\n";
  my $event = new TxProc();
  $event->maxWaitTime( $timeout ) if(defined($timeout));
  my $bReadyToRead = $event->waitForData( $readSocket );
  if( $bReadyToRead )
  {
    my ($resultObj,$errString) = TxProc->unSerialise( $readSocket );
    if( !$resultObj )
    {
      print LOGFILE "$timestamp WARN waitForResults: failed to deserialise:$errString\n";
      $this->printResultToSocket( $readSocket, 'none', 0 ) if($this->{type} ne 'localDgram');
      return (undef,$errString);
    } # if
    else
    {
      print LOGFILE "$timestamp INFO waitForResults: ".$resultObj->toString()."\n" if($this->{beVerbose});
      $this->printResultToSocket( $readSocket, $resultObj->reference(), 1 ) if($this->{type} ne 'localDgram');
      return ($resultObj,undef);
    } # else

  } # if
  else
  {
    print LOGFILE "$timestamp INFO waitForResults: waitForData aborted waiting\n";
    return (undef,'aborted waiting');
  } # else

  close( $readSocket ) if(defined($readSocket));
  close( $this->{socket} ) if($this->{type} ne 'localDgram');
} # waitForResults
