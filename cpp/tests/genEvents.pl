#!/usr/bin/perl -w
# $Id: genEvents.pl 1 2009-08-17 12:36:47Z gerhardus $
#
# Generates a test load for mserver
# support multiple queues with mixed load types
# rate / queue adjustable
# ceiling on the number of events for a queue
#
# Gerhardus Muller
#

use strict;
use Getopt::Std;
use IO::Socket;
use IO::Select;
use Time::localtime;
use Time::HiRes qw( usleep gettimeofday tv_interval );
use POSIX qw( setsid );
use lib '/home/hylafax/faxFx2/perlModules';
use lib '/home/hylafax/platformV3/perlModules';
use Log qw( openLog closeLog $timestamp $logYear $logMonth $logDay *LOGFILE );
use DispatchScriptEvent;
use DispatchNotifyEvent;
use DispatchRequestEvent;
use DispatchCommandEvent;
use EventArchive;

# parse the command line options
my  $bDetach = 0;
my  $pidFile = '/var/run/genEvents.pid';
my  $logFile = "/var/log/genEvents.log";
our $timeToDie = 0;
our $beVerbose = 0;
our $cycleNo = 0;                 # global cycle number that can be accessed from generateEvent evals
our @msnArray = ();               # for use in genRandMSN
our $numMSNs = 0;
our @socketArray;                 # supports an array of connected sockets
our @filenoArray;                 # corresponding file numbers for the select
our %reverseSocketLookup;         # to get from the socket object to its index
our $nextWriteSocket = 0;         # index into @socketArray
our $bUseSocketArray = 0;
our $bRepliesRequested = 0;       # true if $stream->{requestReply} is set for at least one stream - $numTCPRepliesOutstanding will be used to count
our $numTCPRepliesOutstanding = 0;
our $selectObject;
our %referenceHash;               # used to correlate requests and responses when using the socket array
our $referenceMismatches = 0;
our $referencesNotFound = 0;
our $totalReferencesChecked = 0;
my  $loopInterval = 100;          # in ms
my  $loopCorrection = 0;          # correction in ms to get the average rate closer to real
my  %option = ();
getopts( "hvc:d", \%option );
# -v is verbose
$beVerbose = 1 if( exists( $option{v} ) );
$bDetach = 1 if( exists( $option{d} ) );
# -h asks for help
if( exists( $option{h} ) )
{
  print( "$0 options\n" );
  print( "\t-d to detach and run as a daemon\n" );
  print( "\t-c config file\n" );
  print( "\t-v for verbose output\n" );
  print( "\t-h for this help screen\n" );
  print( "\nRefer to the document testing.txt for a description of the tests\n" );
  exit( 1 );
} # help

# run as a daemon if requested
my $pid;
if( $bDetach )
{
  $pid = fork;
  if( $pid )
  {
    system( "echo $pid > $pidFile" );
    print( "$0 has pid $pid\n" );
    exit;
  }
  die "$0 could not fork $!" unless defined( $pid );
  POSIX::setsid( ) or die "$0 cannot start a new session: $!";

  # redirect output to a log file
  open( LOG, ">", $logFile ) or die "Cannot open logfile $logFile: $!";
} # if bDetach
else
{
  open( LOG, ">&STDOUT" );
} # if

$SIG{INT} = $SIG{TERM} = \&signalHandler;
my $configFile = 'genEvents.cfg';
$configFile = $option{c} if( exists( $option{c} ) );
print LOG "using config file '$configFile' pid $$\n";

# datastructure describing the events to be generated
my ($mainParams,$streams) = readConfig( $configFile );

$loopInterval = $mainParams->{interval} if( exists( $mainParams->{interval} ) );
$loopCorrection = $mainParams->{loopCorrection} if( exists( $mainParams->{loopCorrection} ) );
print LOG "interval: $loopInterval ms loopCorrection: $loopCorrection\n";

genMSNArray();

# setup the event generation variables
my @cycleNumber;
my @nextSeqNumber;
my @interval;
my @jitter;
my @seqIncrement;
my @numEventsPerCycle;
my @eventCount;
my @maxNumEvents;
my $streamNo = 0;
print LOG "\nstream summary:\n";
foreach my $stream (@$streams)
{
  $stream->{interval}=1000/$stream->{rate} if( exists($stream->{rate}) && ($stream->{rate}>0) );
  die "define either interval or rate for stream $streamNo" if( !exists($stream->{interval} ) );
  die "define type for stream $streamNo" if( !exists($stream->{type} ) );
  if( ($stream->{type} eq 'dispatchRequestEvent') || ($stream->{type} eq 'dispatchNotifyEvent') )
  {
    die "define url for stream $streamNo" if( !exists($stream->{url} ) );
  } # if
  elsif( $stream->{type} eq 'dispatchScriptEvent' )
  {
    die "define script,scriptType for stream $streamNo" if( !exists($stream->{script}) || !exists($stream->{scriptType}) );
    # fix the location of the script to the current dir if not explicitly specified
    my $script = $stream->{script};
    if( $script !~ /\// )
    {
      my $curDir = `pwd`;
      chomp $curDir;
      $stream->{script} = $curDir."/".$script;
    } # if
  } # if
  verifyTransport( $stream );
  if( ($stream->{persistentpool} > 0) && ($stream->{transport} eq 'tcp') )
  {
    print LOG "stream $streamNo creating a pool of $stream->{persistentpool} tcp connection objects, requestReply: $stream->{requestReply}\n";
    createTCPSocketArray( $stream->{persistentpool}, $stream->{ip}, $stream->{service} );
  } # if

  $interval[$streamNo] = $stream->{interval};
  $jitter[$streamNo] = 0;
  $jitter[$streamNo] = $stream->{jitter} if( exists($stream->{jitter}) );
  $cycleNumber[$streamNo] = 0;
  $numEventsPerCycle[$streamNo] = int(2*$loopInterval / $interval[$streamNo] + 0.5);
  $numEventsPerCycle[$streamNo] = 1 if( $numEventsPerCycle[$streamNo] < 1 );
  $seqIncrement[$streamNo] = int($numEventsPerCycle[$streamNo]*$interval[$streamNo] / $loopInterval + 0.5);
  $nextSeqNumber[$streamNo] = $cycleNumber[$streamNo] * $seqIncrement[$streamNo];
  $maxNumEvents[$streamNo] = 0;
  $maxNumEvents[$streamNo] = $stream->{maxNumEvents} if( exists($stream->{maxNumEvents}) );
  $eventCount[$streamNo] = 0;
  $bRepliesRequested = 1 if( exists($stream->{requestReply}) );

  # print the stream
  print LOG "stream $streamNo: interval $interval[$streamNo] ms, seqIncrement $seqIncrement[$streamNo], numEventsPerCycle: $numEventsPerCycle[$streamNo], jitter $jitter[$streamNo] ms, maxNumEvents $maxNumEvents[$streamNo]\n";
  foreach my $key (sort keys %$stream)
  {
    print LOG "  '$key' => '$stream->{$key}'\n";
  } # foreach

  $streamNo++;
} #foreach
my $numStreams = $streamNo;

# main loop that generates the events
my $startSecs = time( );
my $seqno = 0;
my $streamsDone = 0;
print LOG "\nrunning\n";
while( !$timeToDie )
{
  # generate the events
  for( $streamNo = 0; $streamNo < $numStreams; $streamNo++ )
  {
    if( ( $nextSeqNumber[$streamNo] <= $seqno ) && ($nextSeqNumber[$streamNo] != -1) )
    {
      my $stream = $streams->[$streamNo];
      for( my $i = 0; $i < $numEventsPerCycle[$streamNo]; $i++ )
      {
        generateEvent( $stream, $cycleNumber[$streamNo] );
        $eventCount[$streamNo]++;
      } # for
      print LOG "stream $streamNo: type: $stream->{type} cycleNo: $cycleNumber[$streamNo]\n";
      $cycleNumber[$streamNo]++;
      if( ($maxNumEvents[$streamNo]>0) && ($cycleNumber[$streamNo]*$numEventsPerCycle[$streamNo] >= $maxNumEvents[$streamNo]) )
      {
        $nextSeqNumber[$streamNo] = -1;
        $streamsDone++;
      } # if
      else
      {
        $nextSeqNumber[$streamNo] = $cycleNumber[$streamNo] * $seqIncrement[$streamNo];
        $nextSeqNumber[$streamNo] += int( rand($jitter[$streamNo]) / $loopInterval + 0.5 ) if( $jitter[$streamNo] > 0 );
      } # else
    } # if
  } # for

  readReadySockets(0) if( $bUseSocketArray );
  $seqno++;
  $timeToDie = 1 if( $streamsDone == $numStreams );
  usleep( ($loopInterval-$loopCorrection)*1000 );
} # while

# print statistics
my $elapsedSecs = time( ) - $startSecs;
$elapsedSecs = 1 if( $elapsedSecs == 0 );
for( $streamNo = 0; $streamNo < $numStreams; $streamNo++ )
{
  print LOG "stats for stream $streamNo:\n";
  if( $eventCount[$streamNo] > 0 )
  {
    my $rate = $eventCount[$streamNo] / $elapsedSecs;
    print LOG "  generated $eventCount[$streamNo] events in $elapsedSecs secs or $rate events per second\n";
  } # if
  else
  {
    print LOG "  no events\n";
  } # else
} # for

# read all the replies if they were requested
if( $bRepliesRequested && ($numTCPRepliesOutstanding>0) )
{
  $timeToDie = 0;
  print LOG "waiting for $numTCPRepliesOutstanding outstanding replies\n";
  my $maxTimeout = 60;
  while( ($numTCPRepliesOutstanding>0) && ($maxTimeout>0) && !$timeToDie )
  {
    print LOG "reading $maxTimeout\n";
    readReadySockets(1);
    $maxTimeout--;
  } # while
} # if
else
{
  print LOG "no outstanding replies\n";
} # else
print LOG "references not found:$referencesNotFound not on same socket:$referenceMismatches total:$totalReferencesChecked outstanding:$numTCPRepliesOutstanding\n";
exit 0;
######
######

# generates an event
sub generateEvent
{
  my $stream;
  my $event;
  ($stream,$cycleNo) = @_;
  if( ($stream->{type} eq 'dispatchRequestEvent') || ($stream->{type} eq 'dispatchNotifyEvent') )
  {
    if( $stream->{type} eq 'dispatchRequestEvent' )
    {
      $event = new DispatchRequestEvent();
    } # if dispatchRequestEvent
    else
    {
      $event = new DispatchNotifyEvent();
    } # else dispatchNotifyEvent
    $event->url( $stream->{url} );

    # add parameters
    my $paramNo = 0;
    while( exists($stream->{"param$paramNo"}) )
    {
      if( $stream->{"param$paramNo"} =~ /^\s*(\w+)\s*=\s*(.+)/ )
      {
        my $name = $1;
        my $value = $2;
        # if value is a function implement
        if( $value =~ /^%%(.+)%%/ )
        {
          $value = eval( $1 );
        } # if
        $event->addParam( $name, $value );
      } # if
      else
      {
        warn "generateEvent: unable to parse url parameter $paramNo: '".$stream->{"param$paramNo"}."'\n";
      } # eles
      $paramNo++;
    } # while
  } # if dispatchRequestEvent
  elsif( $stream->{type} eq 'dispatchScriptEvent' )
  {
    $event = new DispatchScriptEvent();
    $event->scriptName( $stream->{script} );
    $event->execType( $stream->{scriptType} );

    # add parameters
    my $paramNo = 0;
    while( exists($stream->{"param$paramNo"}) )
    {
      my $value = $stream->{"param$paramNo"};
      # if value is a function implement
      if( $value =~ /^%%(.+)%%/ )
      {
        $value = eval( $1 );
        print LOG "dispatchScriptEvent: param$paramNo eval '$1' value '$value'\n" if( $beVerbose );
      } # if
      $event->addParam( $value );
      $paramNo++;
    } # while
  } # elsif dispatchScriptEvent

  $event->destQueue( $stream->{queue} );
  $event->lifeTime( $stream->{lifeTime} ) if( exists($stream->{lifeTime}) );
  if( exists($stream->{readyTime}) )
  {
    my $readyTime = $stream->{readyTime};
    if( $readyTime =~ /^%%(.+)%%/ )
    {
      $readyTime = eval( $1 );
      print LOG "dispatchScriptEvent: readyTime eval '$1' value '$readyTime'\n" if( $beVerbose );
    } # if
    $event->readyTime( $readyTime );
  } # if
  if( exists($stream->{requestReply}) )
  {
    my $requestReply = $stream->{requestReply};
    $event->returnFd( 0 ) if( $requestReply );
  } # if
  if( exists($stream->{reference}) )
  {
    my $reference = $stream->{reference};
    if( $reference =~ /^%%(.+)%%/ )
    {
      $reference = eval( $1 );
      print LOG "dispatchScriptEvent: ref eval '$1' value '$reference'\n" if( $beVerbose );
    } # if
    $event->reference( $reference );
  } # if
  elsif( $bUseSocketArray )
  {
    # always provide a reference if using the socketArray
    $event->reference( int(rand(10000000000)) );
  } # else

  # submit
  my $retVal;
  my $submitErrorString;
  my $archiveObj = new EventArchive();
  my $objString = $archiveObj->serialiseToString( $event );
  if( $stream->{transport} eq 'unixdomain' )
  {
    ($retVal,$submitErrorString) = $archiveObj->sendUnixDomainPacket( $objString, $stream->{socketpath} );
  } # if unixdomain
  elsif( $stream->{transport} eq 'tcp' )
  {
    if( $bUseSocketArray )
    {
      ($retVal,$submitErrorString) = writeTCPSocketArrayPacket( $objString, $event->reference );
    } # if
    else
    {
      ($retVal,$submitErrorString) = $archiveObj->sendTcpPacket( $objString, $stream->{ip}, $stream->{service} );
    } # else
  } # elsif tcp
  print LOG "generateEvent: failed to send packet: '$submitErrorString'\n" if( !$retVal );
} # generateEvent

sub signalHandler
{
  $timeToDie = 1;
} # signalHandler

# verifies the transport parameters
sub verifyTransport
{
  my ($stream,$streamNumber) = @_;
  die "define transport for stream $streamNo" if( !exists($stream->{transport} ) );
  if( $stream->{transport} eq 'unixdomain' )
  {
    die "define socketpath for stream $streamNo" if( !exists($stream->{socketpath} ) );
  } # if unixdomain
  elsif( $stream->{transport} eq 'tcp' )
  {
    die "define ip,service for stream $streamNo" if( !exists($stream->{ip}) || !exists($stream->{service} ) );
  } # elsif tcp
  else
  {
    die "transport '$stream->{transport}' for stream $streamNo not supported";
  } # else
} # verifyTransport

# read the config file into a datastructure to describe the streams
# a stream is described by:
#   a rate
#   jitter on the rate
#   an event type
#   a queue to service it
#   a transport
sub readConfig
{
  my $confName = shift;
  my %mainParams;
  my @streams;

  if( !open( CONFFILE, "<", $confName ) )
  {
    warn( "readConfig: failed to open logfile '$confName'" );
    return 1;
  } # if
  print LOG "readConfig: parsing config file '$confName'\n";
  
  my $lineNo = 0;
  my $section = '';
  while( <CONFFILE> )
  {
    my $line = $_;
    $lineNo++;
    if( $line =~ /^\[(\w+)]/ )
    {
      $section = $1;
      print LOG "readConfig: processing section '$section'\n";
    } # if
    elsif( $section eq "main" )
    {
      if( $line =~ /^\s*(\w+)\s*=\s*(.+)/ )
      {
        $mainParams{$1} = $2;
      } # if
      else
      {
        chomp( $line );
        warn "readConfig: unable to parse line $lineNo section $section: '$line'\n" if( ( length($line) > 0 ) && ($line !~ /^\s*#/));
      } # else
    } # elsif
    elsif( $section =~ /^stream(\d+)/ )
    {
      my $streamIndex = $1;
      if( $line =~ /^\s*(\w+)\s*=\s*(.+)/ )
      {
        $streams[$streamIndex]{$1} = $2;
      } # if
      else
      {
        chomp( $line );
        warn "readConfig: unable to parse line $lineNo section $section: '$line'\n" if( ( length($line) > 0 ) && ($line !~ /^\s*#/));
      } # else
    } # if
  } # while
  close( CONFFILE );

  # dump the contents of mainParams
  print LOG "\nreadConfig: parsed config file contents:\n";
  foreach my $key (keys %mainParams)
  {
    print LOG "readConfig: [main] '$key' = '$mainParams{$key}'\n";
  } # foreach


  # dump the contents of the streams
  my $streamNo = 0;
  foreach my $stream (@streams)
  {
    foreach my $key (keys %$stream)
    {
      print LOG "readConfig: [stream$streamNo] '$key' = '$stream->{$key}'\n";
    } # foreach
    $streamNo++;
  } # foreach

  print LOG "\n";
  return (\%mainParams, \@streams);
} # readConfig

######
# generateEvent eval functions
# generates a random msn
sub genRandMSN
{
  return $msnArray[int(rand($numMSNs+0.99))];
} # genRandMSN

sub genMSNArray
{
  my $range;
  $range = enumerateRange( '800000', 100000 );
  push( @msnArray, @$range );
  $range = enumerateRange( '8000', 1000 );
  push( @msnArray, @$range );
  $range = enumerateRange( '9000', 1000 );
  push( @msnArray, @$range );
  $range = enumerateRange( '900000', 90000 );
  push( @msnArray, @$range );
  $range = enumerateRange( '6562769', 4 );
  push( @msnArray, @$range );
  $range = enumerateRange( '6157172', 1 );
  push( @msnArray, @$range );
  $range = enumerateRange( '6157174', 1 );
  push( @msnArray, @$range );
  $range = enumerateRange( '500000', 30000 );
  push( @msnArray, @$range );
  $range = enumerateRange( '530000', 5000 );
  push( @msnArray, @$range );
  $range = enumerateRange( '535000', 5000 );
  push( @msnArray, @$range );
  $range = enumerateRange( '540000', 5000 );
  push( @msnArray, @$range );
  $range = enumerateRange( '545000', 10000 );
  push( @msnArray, @$range );
  $range = enumerateRange( '555000', 10000 );
  push( @msnArray, @$range );
  $range = enumerateRange( '565000', 10000 );
  push( @msnArray, @$range );
  $range = enumerateRange( '575000', 10000 );
  push( @msnArray, @$range );
  $numMSNs = @msnArray;
  print LOG "genMSNArray: generated a total of $numMSNs MSNs\n";
#  print LOG "genMSNArray: ".join(',',@msnArray)."\n";
} # genMSNArray

sub enumerateRange
{
  my ($start, $num) = @_;
  my @range;
  $#range = $num-1;       # pre-extend the array
  my $numberLen = length( $start );
  for( my $i = 0; $i < $num; $i++ )
  {
    my $newNum = substr( "0000000000".$start++, -$numberLen );
    $range[$i] = $newNum;
  } # for

  return \@range;
} # enumerateRange

#####
# support for using an array of tcp sockets that remain connected and will read
# and log returned values
sub createTCPSocketArray
{
  my ($numSockets,$ip,$service) = @_;
  $selectObject = new IO::Select;

  for( my $i = 0; $i < $numSockets; $i++ )
  {
    my $socket = IO::Socket::INET->new( PeerAddr  => $ip,
      PeerPort  => $service,
      Proto     => 'tcp',
      Type      => SOCK_STREAM,
      Blocking  => 0 );
    die( "IO::Socket::INET->new failed - server:$ip, service:$service - $@" ) if( !$socket );
    $socketArray[$i] = $socket;
    $reverseSocketLookup{$socket} = $i;
    $filenoArray[$i] = $socket->fileno;
    $selectObject->add( $socket );

    # read the socket greeting
    my $len;
    my $greeting;
    die( "createTCPSocketArray - read greeting headerlength failed on socket $i - $!" ) if( !defined( $socket->read( $len, 3 ) ) );
    die( "createTCPSocketArray - read greeting failed on socket $i - $!" ) if( !defined( $socket->read( $greeting, $len+1 ) ) );
    $greeting = substr( $greeting, 1 );  # remove the ':'
    print LOG "createTCPSocketArray socket $i greeting: '$greeting'\n";
  } # for
  $bUseSocketArray = 1;
} # createTCPSocketArray

sub writeTCPSocketArrayPacket
{
  my ($packet,$ref) = @_;
  my $socketI = $nextWriteSocket;
  my $socket = $socketArray[$nextWriteSocket++];
  $nextWriteSocket = 0 if( $nextWriteSocket == @socketArray );

  $referenceHash{$ref} = $socketI; 
  my $retVal = $socket->send( $packet, 0 );
  return (0, "writeTCPSocketArrayPacket send failed on socket: $socketI - $@\n" ) if( !$retVal );
  $numTCPRepliesOutstanding++ if( $bRepliesRequested );

  # read the reply if available
  readReadySockets(0);
  return ($retVal, 'success');
} # writeTCPSocketArrayPacket

# read the reply from sockets and associate with the request if possible
# @param waitTime in s
sub readReadySockets
{
  my $waitTime = shift;
  my @ready;
  my $numRead = 0;
  my $eventObj = new EventArchive;

  if( @ready = $selectObject->can_read( $waitTime ) )
  {
    foreach my $socket (@ready)
    {
      # read reply packet
      # my $socket = $socketArray[$socketI];
      my $socketI = $reverseSocketLookup{$socket};
      my $type = 1;  # to start the loop
      my $packet;
      while( $type > 0 )
      {
        ($type,$packet) = $eventObj->readReplyPacket( $socket, 0 );
        next if( $type == 0 );

        $numRead++ if( $type > 0 );
        my $resultType = EventArchive->getReplyTypeString( $type );
        if( $type == 1 )
        {
          $packet =~ s/\n/\\n/g;
          print LOG "readReadySockets socket: $socketI read type: '$resultType' result:'$packet'\n" if( $beVerbose );
        } # if type 1
        elsif( $type == 2 ) # it is a response object rather than a success/fail reply
        {
          $numTCPRepliesOutstanding-- if( $bRepliesRequested );

          my ($obj,$error) = $eventObj->deserialise( $packet );
          if( $obj )
          {
            my $ref = $obj->reference();
            my $origSocketI = -1;
            my $bMismatch = 0;
            if( exists($referenceHash{$ref}) )
            {
              $origSocketI = $referenceHash{$ref};
              delete $referenceHash{$ref};
              if( $socketI != $origSocketI )
              {
                $referenceMismatches++;
                $bMismatch = 1;
              } # if
            } # if
            else
            {
              $referencesNotFound++;
              $bMismatch = 1;
            } # else
            $totalReferencesChecked++;
            print LOG "readReadySockets socket: $socketI read type: '$resultType' ref: '$ref' mismatch:$bMismatch socketI:$socketI origSocketI:$origSocketI\n" if( $beVerbose );
          } # if
          else
          {
            print LOG "readReadySockets socket: $socketI read type: '$resultType' failed to unserialise object: $error\n";
          } # else
        } # if
      }  # while
    } # foreach
  } # if
  return $numRead;
} # readReadySockets


