# txProc interface class - mirrors the C++ baseEvent class
#
# $Id: TxProc.pm 2946 2013-12-10 10:00:08Z gerhardus $
# Gerhardus Muller
# Versioning: a.b.c a is a major release, b represents changes or new features, c represents bug fixes. 
# @version 1.0.0		20/11/2009		Gerhardus Muller		implements txProc version 3 protocol base on Json
# @version 1.0.1		22/04/2010		Gerhardus Muller		replace CMD_DUMMY_1 with CMD_END_OF_QUEUE
# @version 1.0.2		02/08/2010		Gerhardus Muller		toString handling of execParams as array fixed
# @version 1.1.0		14/02/2011		Gerhardus Muller		added a destServer parameter
# @version 1.2.0		24/02/2011		Gerhardus Muller		utf8 encode all strings before assembling the serialised packet
# @version 1.2.1		07/09/2011		Gerhardus Muller		force properties such as retries,lifetime,expiryTime and expiry to be numbers
# @version 1.3.0		01/08/2012		Gerhardus Muller		fixed the serialisation code to handle a persistent unix domain socket to txProc
# @version 1.4.0		01/10/2012		Gerhardus Muller		added workerPid(wpid)
# @version 1.5.0		28/02/2013		Gerhardus Muller		added a check on the max length of header fields
# @version 1.6.0    25/04/2013    Gerhardus Muller    socket error reporting changed to use $!
# @version 1.7.0    13/05/2013    Gerhardus Muller    changed ref logging to omit the single colon
# @version 1.8.0		20/07/2013		Gerhardus Muller		switched to the UTF-8 versions encode_json and decode_json
# @version 1.9.0		18/10/2013		Gerhardus Muller		changed bExpectReply to return a normal 0/1 rather than a JSON::XS::TRUE
# @version 1.10.0		26/11/2013		Gerhardus Muller		fixed unserialise to handle fragmented packets
#
# perl -MCPAN -e "install JSON::XS"
#
package TxProc;
use strict;
use JSON;
use IO::Handle;
use IO::Select;
use IO::Socket;
#use Devel::StackTrace;
use Utils qw( hashToString listToString );
use Log qw( $timestamp *LOGFILE );

my $FRAME_HEADER = '#frameNewframe#v';
my $PROTOCOL_VERSION_NUMBER = '3.0';
my $FRAME_HEADER_LEN = 27; # strlen(FRAME_HEADER)+strlen(PROTOCOL_VERSION_NUMBER)+8 - the 8 is ':%06u\n'
my $BLOCK_HEADER_LEN = 39;
#our $READ_BUF_SIZE = 4096;
our $READ_BUF_SIZE = 32768;
our $MAX_HEADER_BLOCK_LEN = 999999;   # corresponds to %06d
our $defaultVerboseSetting = 0;

# constructor
sub new
{
  my ($class,$type) = @_;

  # call the parent constructor
  my $self = {
    eventType => 0,
    part1 => {},
    part2 => {},
    sysParams => {},
    execParams => {},
    maxWaitTime => 0,
    selectObject => 0,
    beVerbose => $defaultVerboseSetting,
    json => JSON::XS->new->utf8
  };

  bless $self, $class;
  $self->eventType($type) if( defined($type) );
  return $self;
} # sub new

# for debug use on the json objects
sub prettyPrint
{
  my ($this) = @_;
  $this->{json}->pretty;
} # prettyPring
sub maxWaitTime
{
  my ($this,$val) = @_;
  $this->{maxWaitTime} = $val if( defined($val) );
  return $this->{maxWaitTime};
} # maxWaitTime
sub beVerbose
{
  my ($this,$val) = @_;
  $this->{beVerbose} = $val if( defined($val) );
  return $this->{beVerbose};
} # beVerbose
sub destServer
{
  my ($this,$val) = @_;
  $this->{destServer} = $val if( defined($val) );
  return undef if( !exists($this->{destServer}) );
  return $this->{destServer};
} # destServer

# part1 properties eventType,reference,returnFd,destQueue
sub eventType
{
  my ($this,$val) = @_;
  if( defined($val) )
  {
    if( $val eq 'EV_UNKNOWN' )
    {
      $this->{eventType} = 0;
    } # if
    elsif( $val eq 'EV_BASE' )
    {
      $this->{eventType} = 1;
    } # elsif
    elsif( $val eq 'EV_SCRIPT' )
    {
      $this->{eventType} = 2;
    } # elsif
    elsif( $val eq 'EV_PERL' )
    {
      $this->{eventType} = 3;
    } # elsif
    elsif( $val eq 'EV_BIN' )
    {
      $this->{eventType} = 4;
    } # elsif
    elsif( $val eq 'EV_URL' )
    {
      $this->{eventType} = 5;
    } # elsif
    elsif( $val eq 'EV_RESULT' )
    {
      $this->{eventType} = 6;
    } # elsif
    elsif( $val eq 'EV_WORKER_DONE' )
    {
      $this->{eventType} = 7;
    } # elsif
    elsif( $val eq 'EV_COMMAND' )
    {
      $this->{eventType} = 8;
    } # elsif
    elsif( $val eq 'EV_REPLY' )
    {
      $this->{eventType} = 9;
    } # elsif
    elsif( $val eq 'EV_ERROR' )
    {
      $this->{eventType} = 10;
    } # elsif
    else
    {
      warn "TxProc::command unknown eventType '$val'\n";
    } # else
  } # if
  my @textVal = ('EV_UNKNOWN','EV_BASE','EV_SCRIPT','EV_PERL','EV_BIN',
    'EV_URL','EV_RESULT','EV_WORKER_DONE','EV_COMMAND','EV_REPLY','EV_ERROR');
  return $textVal[$this->{eventType}];
} # eventType
sub reference 
{
  my ($this, $val) = @_;
  $this->{part1}->{reference} = $val if defined($val);
  return $this->{part1}->{reference} if( exists($this->{part1}->{reference}) );
  return undef;
} # sub reference
sub returnFd
{
  my ($this, $val) = @_;
  $this->{part1}->{returnFd} = $val if defined($val);
  return $this->{part1}->{returnFd} if( exists($this->{part1}->{returnFd}) );
  return undef;
} # sub returnFd
sub destQueue
{
  my ($this, $val) = @_;
  $this->{part1}->{destQueue} = $val if defined($val);
  return $this->{part1}->{destQueue} if( exists($this->{part1}->{destQueue}) );
  return undef;
} # sub destQueue

# part2 properties - trace,traceTimestamp,expiryTime,lifetime,retries,workerPid(wpid)
sub trace
{
  my ($this, $val) = @_;
  $this->{part2}->{trace} = $val if defined($val);
  return $this->{part2}->{trace} if( exists($this->{part2}->{trace}) );
  return undef;
} # sub trace
sub traceTimestamp
{
  my ($this, $val) = @_;
  $this->{part2}->{traceTimestamp} = $val if defined($val);
  return $this->{part2}->{traceTimestamp} if( exists($this->{part2}->{traceTimestamp}) );
  return undef;
} # sub traceTimestamp
sub expiryTime
{
  my ($this, $val) = @_;
  $this->{part2}->{expiryTime} = $val if defined($val);
  return $this->{part2}->{expiryTime} if( exists($this->{part2}->{expiryTime}) );
  return undef;
} # sub expiryTime
sub lifetime
{
  my ($this, $val) = @_;
  $this->{part2}->{lifetime} = $val if defined($val);
  return $this->{part2}->{lifetime} if( exists($this->{part2}->{lifetime}) );
  return undef;
} # sub lifetime
sub retries
{
  my ($this, $val) = @_;
  $this->{part2}->{retries} = $val if defined($val);
  return $this->{part2}->{retries} if( exists($this->{part2}->{retries}) );
  return undef;
} # sub retries
sub wpid
{
  my ($this, $val) = @_;
  $this->{part2}->{wpid} = $val if defined($val);
  return $this->{part2}->{wpid} if( exists($this->{part2}->{wpid}) );
  return undef;
} # sub wpid

# sysParams - bStandardResponse,command,url,scriptName,result,bSuccess,bExpectReply,
# errorString,failureCause,systemParam,elapsedTime,bGeneratedRecoveryEvent
# 
sub bStandardResponse
{
  my ($this, $val) = @_;
  $this->{sysParams}->{bStandardResponse} = $val if defined($val);
  return $this->{sysParams}->{bStandardResponse} if( exists($this->{sysParams}->{bStandardResponse}) );
  return undef;
} # sub bStandardResponse
sub command
{
  my ($this, $val) = @_;
  if( defined($val) )
  {
    if( $val eq 'CMD_NONE' )
    {
      $this->{sysParams}->{command} = 0;
    } # if
    elsif( $val eq 'CMD_STATS' )
    {
      $this->{sysParams}->{command} = 1;
    } # elsif
    elsif( $val eq 'CMD_RESET_STATS' )
    {
      $this->{sysParams}->{command} = 2;
    } # elsif
    elsif( $val eq 'CMD_REOPEN_LOG' )
    {
      $this->{sysParams}->{command} = 3;
    } # elsif
    elsif( $val eq 'CMD_REREAD_CONF' )
    {
      $this->{sysParams}->{command} = 4;
    } # elsif
    elsif( $val eq 'CMD_EXIT_WHEN_DONE' )
    {
      $this->{sysParams}->{command} = 5;
    } # elsif
    elsif( $val eq 'CMD_SEND_UDP_PACKET' )
    {
      $this->{sysParams}->{command} = 6;
    } # elsif
    elsif( $val eq 'CMD_TIMER_SIGNAL' )
    {
      $this->{sysParams}->{command} = 7;
    } # elsif
    elsif( $val eq 'CMD_CHILD_SIGNAL' )
    {
      $this->{sysParams}->{command} = 8;
    } # elsif
    elsif( $val eq 'CMD_APP' )
    {
      $this->{sysParams}->{command} = 9;
    } # elsif
    elsif( $val eq 'CMD_SHUTDOWN' )
    {
      $this->{sysParams}->{command} = 10;
    } # elsif
    elsif( $val eq 'CMD_NUCLEUS_CONF' )
    {
      $this->{sysParams}->{command} = 11;
    } # elsif
    elsif( $val eq 'CMD_DUMP_STATE' )
    {
      $this->{sysParams}->{command} = 12;
    } # elsif
    elsif( $val eq 'CMD_NETWORKIF_CONF' )
    {
      $this->{sysParams}->{command} = 13;
    } # elsif
    elsif( $val eq 'CMD_END_OF_QUEUE' )
    {
      $this->{sysParams}->{command} = 14;
    } # elsif
    elsif( $val eq 'CMD_MAIN_CONF' )
    {
      $this->{sysParams}->{command} = 15;
    } # elsif
    elsif( $val eq 'CMD_PERSISTENT_APP' )
    {
      $this->{sysParams}->{command} = 16;
    } # elsif
    elsif( $val eq 'CMD_EVENT' )
    {
      $this->{sysParams}->{command} = 17;
    } # elsif
    elsif( $val eq 'CMD_WORKER_CONF' )
    {
      $this->{sysParams}->{command} = 18;
    } # elsif
    else
    {
      warn "TxProc::command unknown command '$val'\n";
    } # else
  } # if
  my @textVal = ('CMD_NONE','CMD_STATS','CMD_RESET_STATS','CMD_REOPEN_LOG','CMD_REREAD_CONF',
    'CMD_EXIT_WHEN_DONE','CMD_SEND_UDP_PACKET','CMD_TIMER_SIGNAL','CMD_CHILD_SIGNAL','CMD_APP',
    'CMD_SHUTDOWN','CMD_NUCLEUS_CONF','CMD_DUMP_STATE','CMD_NETWORKIF_CONF','CMD_END_OF_QUEUE',
    'CMD_MAIN_CONF','CMD_PERSISTENT_APP','CMD_EVENT','CMD_WORKER_CONF');
  return $textVal[$this->{sysParams}->{command}] if( exists($this->{sysParams}->{command}) && defined($this->{sysParams}->{command}) );
  return 'CMD_NONE';
} # sub command
sub url
{
  my ($this, $val) = @_;
  $this->{sysParams}->{url} = $val if defined($val);
  return $this->{sysParams}->{url} if( exists($this->{sysParams}->{url}) );
  return undef;
} # sub url
sub scriptName
{
  my ($this, $val) = @_;
  $this->{sysParams}->{scriptName} = $val if defined($val);
  return $this->{sysParams}->{scriptName} if( exists($this->{sysParams}->{scriptName}) );
  return undef;
} # sub scriptName
sub result
{
  my ($this, $val) = @_;
  $this->{sysParams}->{result} = $val if defined($val);
  return $this->{sysParams}->{result} if( exists($this->{sysParams}->{result}) );
  return undef;
} # sub result
sub bSuccess
{
  my ($this, $val) = @_;
  $this->{sysParams}->{bSuccess} = $val if defined($val);
  return $this->{sysParams}->{bSuccess} if( exists($this->{sysParams}->{bSuccess}) );
  return undef;
} # sub bSuccess
sub bExpectReply
{
  my ($this, $val) = @_;
  $this->{sysParams}->{bExpectReply} = $val if defined($val);
  return ($this->{sysParams}->{bExpectReply})?1:0 if( exists($this->{sysParams}->{bExpectReply}) );
  return undef;
} # sub bExpectReply
sub errorString
{
  my ($this, $val) = @_;
  $this->{sysParams}->{errorString} = $val if defined($val);
  return $this->{sysParams}->{errorString} if( exists($this->{sysParams}->{errorString}) );
  return undef;
} # sub errorString
sub failureCause
{
  my ($this, $val) = @_;
  $this->{sysParams}->{failureCause} = $val if defined($val);
  return $this->{sysParams}->{failureCause} if( exists($this->{sysParams}->{failureCause}) );
  return undef;
} # sub failureCause
sub systemParam
{
  my ($this, $val) = @_;
  $this->{sysParams}->{systemParam} = $val if defined($val);
  return $this->{sysParams}->{systemParam} if( exists($this->{sysParams}->{systemParam}) );
  return undef;
} # sub systemParam
sub elapsedTime
{
  my ($this, $val) = @_;
  $this->{sysParams}->{elapsedTime} = $val if defined($val);
  return $this->{sysParams}->{elapsedTime} if( exists($this->{sysParams}->{elapsedTime}) );
  return undef;
} # sub elapsedTime
sub bGeneratedRecoveryEvent
{
  my ($this, $val) = @_;
  $this->{sysParams}->{bGeneratedRecoveryEvent} = $val if defined($val);
  return $this->{sysParams}->{bGeneratedRecoveryEvent} if( exists($this->{sysParams}->{bGeneratedRecoveryEvent}) );
  return undef;
} # sub bGeneratedRecoveryEvent

# execParams
# named parameters
sub addParam
{
  my ($this,$key,$val) = @_;
  $this->{execParams}->{$key} = $val;
} # addParam
# @param the key of the entry to retrieve
sub getParam
{
  my ($this,$key) = @_;
  if( defined($key) && exists($this->{execParams}->{$key}) )
  {
    return $this->{execParams}->{$key};
  } # if
  else
  {
    return undef;
  } #else
} # getParam
# @param the key of the entry to delete
sub deleteParam
{
  my ($this,$key) = @_;
  delete $this->{execParams}->{$key};
} # deleteParam
sub execParams
{
  my ($this,$val) = @_;
  if( defined($val) )
  {
    if( ref($val) eq "HASH" )
    {
      $this->{execParams} = $val;
    } # if
    else
    {
      my ($package, $filename, $line) = caller;
      my $str = " package:$package filename:$filename line:$line";
      #my $trace = Devel::StackTrace->new;
      #while( my $frame = $trace->next_frame )
      #{
      #  $str .= " Frame:: file:".$frame->filename." line:".$frame->line." sub:".$frame->subroutine;
      #}
      warn "TxProc::execParams supplied value is not a HASH reference: val:'$val' ref:".ref($val)." $str\n";
    } # else
  }
  return $this->{execParams};
} # execParams

# execParams
# positional parameters
# @param the value to add to the end of the list of parameters
sub addScriptParam
{
  my ($this,$val) = @_;
  $this->{execParams} = [] if( ref($this->{execParams}) eq "HASH" ); # default init is for a hash
  push @{$this->{execParams}}, $val;
} # addScriptParam
# @param the value to add to the beginning of the list of parameters
sub prependScriptParam
{
  my ($this,$val) = @_;
  unshift @{$this->{execParams}}, $val;
} # prependScriptParam
# @param the index of the entry to retrieve
sub getScriptParam
{
  my ($this,$index) = @_;
  if( defined($index) && ($index<@{$this->{execParams}}) )
  {
    return @{$this->{execParams}}[$index];
  } # if
  else
  {
    return undef;
  } #else
} # getScriptParam
# @param the index of the entry to set
sub setScriptParam
{
  my ($this,$index,$val) = @_;
  if( defined($index) && ($index<@{$this->{execParams}}) )
  {
    @{$this->{execParams}}[$index] = $val;
  } # if
} # setScriptParam
# @param the index of the value to remove
sub deleteScriptParam
{
  my ($this,$index) = @_;
  splice( @{$this->{execParams}}, $index, 1 );
} # deleteScriptParam
sub shiftScriptParam
{
  my ($this) = @_;
  return shift @{$this->{execParams}};
} # shiftScriptParam

# text representation
sub toString
{
  my $this = shift;
  my $str = $this->eventType()." ";

  # part1
  if( %{$this->{part1}} )
  {
    $str .= "==part1:";
    $str .= "ref:$this->{part1}->{reference} " if(exists($this->{part1}->{reference}) && defined($this->{part1}->{reference}));
    $str .= "returnFd:'$this->{part1}->{returnFd}' " if(exists($this->{part1}->{returnFd}) && defined($this->{part1}->{returnFd}));
    $str .= "destQueue:'$this->{part1}->{destQueue}' " if(exists($this->{part1}->{destQueue}) && defined($this->{part1}->{destQueue}));
  } # if

  $str .= "==sysParams:".hashToString($this->{sysParams}) if(%{$this->{sysParams}});
  if( ref($this->{execParams}) eq "HASH" )
  {
    $str .= "==execParams:".hashToString($this->{execParams}) if(%{$this->{execParams}});
  } # if
  else
  {
    $str .= "==execParams:".listToString($this->{execParams}) if(@{$this->{execParams}}>0);
  } # else

  # part2
  if( %{$this->{part2}} )
  {
    $str .= "==part2:";
    $str .= "trace:'$this->{part2}->{trace}' " if(exists($this->{part2}->{trace}) && defined($this->{part2}->{trace}));
    $str .= "tt:'$this->{part2}->{traceTimestamp}' " if(exists($this->{part2}->{traceTimestamp}) && defined($this->{part2}->{traceTimestamp}));
    $str .= "expiryTime:'$this->{part2}->{expiryTime}' " if(exists($this->{part2}->{expiryTime}) && defined($this->{part2}->{expiryTime}));
    $str .= "lifetime:'$this->{part2}->{lifetime}' " if(exists($this->{part2}->{lifetime}) && defined($this->{part2}->{lifetime}));
    $str .= "retries:'$this->{part2}->{retries}' " if(exists($this->{part2}->{retries}) && defined($this->{part2}->{retries}));
    $str .= "wpid:'$this->{part2}->{wpid}' " if(exists($this->{part2}->{wpid}) && defined($this->{part2}->{wpid}));
  } # if
  return $str;
} # toString

##
# serialisation / deserialisation support
# ##
sub serialisePart1
{
  my ($this) = @_;
  $this->{part1}->{eventType} = $this->{eventType}+0;
  my $jsonStr = '';
  eval
  {
#    $jsonStr = to_json( $this->{part1}, {pretty=>$this->{bPrettyJson}} );
    $jsonStr = $this->{json}->encode( $this->{part1} );
#    utf8::encode( $jsonStr ) if(!utf8::valid($jsonStr));
  }; # eval
  print LOGFILE "$timestamp ERROR TxProc::serialisePart1:to_json barfed:'$@' obj:".$this->toString()."\n" if($@);
  return $jsonStr;
} # serialisePart1
sub serialisePart2
{
  my ($this) = @_;
  my $jsonStr = '';
  eval
  {
    # json c++ code has a hernia if these are not integers
    $this->{part2}->{expiryTime} += 0 if(exists($this->{part2}->{expiryTime}));
    $this->{part2}->{lifetime} += 0 if(exists($this->{part2}->{lifetime}));
    $this->{part2}->{retries} += 0 if(exists($this->{part2}->{retries}));
    $this->{part2}->{wpid} += 0 if(exists($this->{part2}->{wpid}));
#    $jsonStr = to_json( $this->{part2}, {pretty=>$this->{bPrettyJson}} );
    $jsonStr = $this->{json}->encode( $this->{part2} );
#    utf8::encode( $jsonStr ) if(!utf8::valid($jsonStr));
  }; # eval
  print LOGFILE "$timestamp ERROR TxProc::serialisePart2:to_json barfed:'$@' obj:".$this->toString()."\n" if($@);
  return $jsonStr;
} # serialisePart2
sub serialiseSysParams
{
  my ($this) = @_;
  my $jsonStr = '';
  eval
  {
    # json c++ code has a hernia if these are not integers
    $this->{sysParams}->{command} += 0 if(exists($this->{sysParams}->{command}));
    $this->{sysParams}->{bStandardResponse} += 0 if(exists($this->{sysParams}->{bStandardResponse}));
    $this->{sysParams}->{bSuccess} += 0 if(exists($this->{sysParams}->{bSuccess}));
    $this->{sysParams}->{bExpectReply} += 0 if(exists($this->{sysParams}->{bExpectReply}));
    $this->{sysParams}->{elapsedTime} += 0 if(exists($this->{sysParams}->{elapsedTime}));
    $this->{sysParams}->{bGeneratedRecoveryEvent} += 0 if(exists($this->{sysParams}->{bGeneratedRecoveryEvent}));
#    $jsonStr = to_json( $this->{sysParams}, {pretty=>$this->{bPrettyJson}} );
    $jsonStr = $this->{json}->encode( $this->{sysParams} );
#    utf8::encode( $jsonStr ) if(!utf8::valid($jsonStr));
  }; # eval
  print LOGFILE "$timestamp ERROR TxProc::serialiseSysParams:to_json barfed:'$@' obj:".$this->toString()."\n" if($@);
  return $jsonStr;
} # serialiseSysParams
sub serialiseExecParams
{
  my ($this) = @_;
  my $jsonStr = '';
  eval
  {
#    $jsonStr = to_json( $this->{execParams}, {pretty=>$this->{bPrettyJson}} );
    $jsonStr = $this->{json}->encode( $this->{execParams} );
#    utf8::encode( $jsonStr ) if(!utf8::valid($jsonStr));
  }; # eval
  print LOGFILE "$timestamp ERROR TxProc::serialiseExecParams:to_json barfed:'$@' obj:".$this->toString()."\n" if($@);
  return $jsonStr;
} # serialiseExecParams

sub serialiseToString
{
  my ($this) = @_;
  my $objStr = '';
  my $jsonPart1 = $this->serialisePart1();
  my $jsonPart2 = $this->serialisePart2();
  my $jsonSysParams = $this->serialiseSysParams();
  my $jsonExecParams = $this->serialiseExecParams();
  my $payloadLen = $BLOCK_HEADER_LEN+length($jsonPart1)+length($jsonPart2)+length($jsonSysParams)+length($jsonExecParams);

  # not the best fix imaginable but otherwise we break the protocol
  if( $payloadLen>$MAX_HEADER_BLOCK_LEN )
  {
    print LOGFILE "$timestamp WARN TxProc::serialiseToString payloadLen:$payloadLen exceeds max - truncating execParams\n";
    $jsonExecParams = '';
    $payloadLen = $BLOCK_HEADER_LEN+length($jsonPart1)+length($jsonPart2)+length($jsonSysParams)+length($jsonExecParams);
  } # if

  my $payloadHeader = sprintf( "%s%s:%06u\n%02u,1,%06u,1,%06u,1,%06u,1,%06u\n",$FRAME_HEADER,$PROTOCOL_VERSION_NUMBER,
    $payloadLen,4,length($jsonPart1),length($jsonPart2),length($jsonSysParams),length($jsonExecParams) );
  my $strSerialised = $payloadHeader.$jsonPart1.$jsonPart2.$jsonSysParams.$jsonExecParams;
#print LOGFILE "$timestamp DEBUG TxProc::serialiseToString strSerialised:$strSerialised";

  return $strSerialised;
} # serialiseToString

# read the socket greeting for socket streams
sub readGreeting
{
  my ($this,$socket) = @_;
  my $len;
  my $greeting;

  # read '024:'
  return (0, "TxProc::readGreeting - read greeting headerlength failed - $!" ) if( !defined($socket->read($len,4)) );
  chop( $len );  # remove the ':'

  # read the rest of the greeting 'program@host pver x.x'
  return (0, "TxProc::readGreeting - read greeting len:$len failed - $!" ) if( !defined( $socket->read( $greeting, $len ) ) );
  my $pver = '';
  $pver = $1 if( $greeting =~ /pver ([\d.]+)/ );
  return (0, "TxProc::readGreeting failed on protocol version - expected:'$PROTOCOL_VERSION_NUMBER' got:'$pver' greeting:'$greeting'") if( $pver ne $PROTOCOL_VERSION_NUMBER );
  return (1, undef);
} # readGreeting

# read stream based reply
# @return (0,failReason) or (1,'') for no response or (2,'') to expect a response
sub readReply
{
  my ($this,$socket) = @_;
  my ($reply,$err) = $this->unSerialise($socket);
  return (0,$err) if( !$reply );
  return (1+$reply->bExpectReply(),'');
} # readReply

# @return true if data available or false if not
sub waitForData
{
  my ($this,$socket) = @_;
  $this->{selectObject} = new IO::Select($socket) if( $this->{selectObject} == 0 );

  my @ready;
  return 1 if( (@ready=$this->{selectObject}->can_read($this->{maxWaitTime})) );
  return 0;
} # waitForData

# parses the body and populates the object
# @return (1,'') or (0,errStr)
sub parseBody
{
  my ($this,$body) = @_;
  my $numSections = 0;
  my $part1Size = 0;
  my $part2Size = 0;
  my $sysSize = 0;
  my $execSize = 0;
  if( $body =~ /^(\d+),1,(\d+),1,(\d+),1,(\d+),1,(\d+)/ )
  {
    $numSections = $1;
    $part1Size = $2;
    $part2Size = $3;
    $sysSize = $4;
    $execSize = $5;

    return(0, "TxProc::parseBody: expected 4 sections found:$numSections" ) if( $numSections != 4 );
    return(0, "TxProc::parseBody: part1 cannot be empty" ) if( $part1Size == 0 );
  } # if
  else
  { 
    return (0,"TxProc::parseBody: failed to parse body header:'$body'");
  } # else

  print( LOGFILE "$timestamp INFO TxProc::parseBody: part1Size:$part1Size part2Size:$part2Size sysSize:$sysSize execSize:$execSize\nbody:'$body'\n" ) if( $this->{beVerbose} );
  my $startOffset = $BLOCK_HEADER_LEN;

  my $jsonPart1 = substr( $body, $startOffset, $part1Size );
  $startOffset += $part1Size;
#  $this->{part1} = from_json( $jsonPart1 );
  $this->{part1} = $this->{json}->decode( $jsonPart1 );
  $this->{eventType} = $this->{part1}->{eventType};

  if( $part2Size > 0 )
  {
    my $jsonPart2 = substr( $body, $startOffset, $part2Size );
    $startOffset += $part2Size;
#    $this->{part2} = from_json( $jsonPart2 );
    $this->{part2} = $this->{json}->decode( $jsonPart2 );
  } # if

  if( $sysSize > 0 )
  {
    my $jsonSysParams = substr( $body, $startOffset, $sysSize );
    $startOffset += $sysSize;
#    $this->{sysParams} = from_json( $jsonSysParams );
    $this->{sysParams} = $this->{json}->decode( $jsonSysParams );
  } # if

  if( $execSize > 0 )
  {
    my $jsonExecParams = substr( $body, $startOffset, $execSize );
    $startOffset += $execSize;
#    $this->{execParams} = from_json( $jsonExecParams );
    $this->{execParams} = $this->{json}->decode( $jsonExecParams );
  } # if

  return (1,'');
} # parseBody

# static method
# expects the entire packet in the string
# @return new object or (0,errStr)
sub unSerialiseFromString
{
  my ($class,$packet,$beVerbose) = @_;
  my $len;
  my $header;

  # read the header
  $header = substr( $packet, 0, $FRAME_HEADER_LEN );

  # verify protocol version and extract payload length
  my $packetLen = 0;
  my $headerTemplate = sprintf( "^%s%s:(\\d+)", $FRAME_HEADER,$PROTOCOL_VERSION_NUMBER );
  if( $header =~ /$headerTemplate/ )
  {
    $packetLen = $1;
  } # if
  else
  {
    return (0, "TxProc::unSerialise failed to parse the header:'$header'" );
  } # else

  # read the body
  my $body = substr( $packet, $FRAME_HEADER_LEN, $packetLen );

  # deserialise
  my $newObject = new TxProc;
  $newObject->beVerbose(1) if( defined($beVerbose) && $beVerbose );
  my ($retVal,$errStr) = $newObject->parseBody( $body );
  return $newObject if( $retVal );
  return (0,$errStr);
} # unSerialiseFromString

# static method
# precede with a call to waitForData if non-blocking and required to wait
# not coded to properly handle non-blocking sockets - the code would then not throw and return
# the part buffer and ... simply not supported as there is most likely no use case for it at the moment
# @return new object
sub unSerialise
{
  my ($class,$socket,$beVerbose) = @_;
  my $len;
  my $header;

  # read the header
  my $result = $socket->read( $header, $FRAME_HEADER_LEN );  # read the frame header, protocol version and payload length including the \n at the end
  return (0, "TxProc::unSerialise no data" ) if( !defined($result) );
  return (0, "TxProc::unSerialise only read '$result' bytes for the header" ) if( $result!=$FRAME_HEADER_LEN );

  # verify protocol version and extract payload length
  my $packetLen = 0;
  my $headerTemplate = sprintf( "^%s%s:(\\d+)", $FRAME_HEADER,$PROTOCOL_VERSION_NUMBER );
  if( $header =~ /$headerTemplate/ )
  {
    $packetLen = $1;
  } # if
  else
  {
    return (0, "TxProc::unSerialise failed to parse the header:'$header'" );
  } # else

  # read the body
  my $body = '';
  my $readResult = 1;
  while( (length($body)<$packetLen) && $readResult )
  {
    my $partBody;
    $readResult = $socket->read( $partBody, $packetLen );
    $body .= $partBody if(defined($partBody) && $readResult);
  } # while
  return (0, "TxProc::unSerialise read body len:$packetLen failed; read ".length($body)." bytes - $!" ) if( !defined($readResult) || (length($body)!=$packetLen) );

  # deserialise
  my $newObject = new TxProc;
  $newObject->beVerbose(1) if( defined($beVerbose) && $beVerbose );
  my ($retVal,$errStr) = $newObject->parseBody( $body );
  return $newObject if( $retVal );
  return (0,$errStr);
} # unSerialise

# @param $socket - if a socket object is passed in it is assumed it is a TCP/stream socket object
# @param $unixdomainPath
# @param $serverName
# @param $serverService
# @return ((0 fail, 1 success, 2 expect response),error string)
sub serialise
{
  my ($this,$socket,$unixdomainPath,$serverName,$serverService) = @_;

  # serialise and get the actual payload
  my $payload = $this->serialiseToString();
  my $payloadLen = length( $payload );
  print( LOGFILE "$timestamp INFO TxProc::serialise: len:$payloadLen '$payload'\n" ) if($this->{beVerbose});

  # check max length we can receive via unix domain socket
  if( defined($unixdomainPath) && ($payloadLen >= $READ_BUF_SIZE) )
  {
    if( defined($serverName) && defined($serverService) )
    {
      undef $unixdomainPath;
      print( LOGFILE "$timestamp INFO TxProc::serialise: len:$payloadLen exceeds max len:$READ_BUF_SIZE using TCP server:($serverName service:$serverService\n" );
    } # if
    else
    {
      print( LOGFILE "$timestamp INFO TxProc::serialise len:$payloadLen exceeds max len:$READ_BUF_SIZE - no TCP server" );
      return( 0, "TxProc::serialise len:$payloadLen exceeds max len:$READ_BUF_SIZE - no TCP server" );
    } # else
  } # if

  # if we were not given a socket object then create one
  my $retVal = 0;
  my $errorString;
  my $bStreamSocket = 0;
  my $bPipe = 0;
  my $bLocalSocketCreated = 1;
  if( defined($socket) )
  {
    print( LOGFILE "$timestamp DEBUG TxProc::serialise protocol:".($socket->protocol()==PF_INET?'PF_INET':$socket->protocol())." sockdomain:".$socket->sockdomain()." sockettype:".($socket->socktype()==SOCK_DGRAM?'SOCK_DGRAM':'SOCK_STREAM')."\n" ) if($this->{beVerbose});
    $bStreamSocket = 1 if($socket->socktype()==SOCK_STREAM);
    $bLocalSocketCreated = 0;
  } # if
  else
  {
    if( defined($unixdomainPath) )
    {
      $bStreamSocket = 0;
      $socket = IO::Socket::UNIX->new(  Peer    => $unixdomainPath,
                                        Type    => SOCK_DGRAM
                                      );
      return (0, "TxProc::serialise IO::Socket::UNIX->new failed - path:'$unixdomainPath' - $!" ) if( !defined( $socket ) );
    } # if
    elsif( defined($serverName) && ($serverName eq '-') )
    {
      $bStreamSocket = 0;
      $bPipe = 1;
      $socket = new IO::Handle;
      $socket->fdopen( fileno(STDOUT), "w" );
    } # elsif
    elsif( defined($serverName) && defined($serverService) )
    {
      $bStreamSocket = 1;
      $socket = IO::Socket::INET->new( PeerAddr  => $serverName,
        PeerPort  => $serverService,
        Proto     => 'tcp',
        Type      => SOCK_STREAM );
      return (0, "TxProc::serialise IO::Socket::INET->new failed - server:$serverName, service:$serverService - $!" ) if( !$socket );
    } # if
    else
    {
      return (0, "TxProc::serialise no socket definitions available" );
    } # else
  } # else

  # if a stream socket first read the greeting
  if( $bStreamSocket )
  {
    ($retVal,$errorString) = $this->readGreeting( $socket );
    if( !$retVal )
    {
      close($socket) if( $bLocalSocketCreated );
      return ($retVal,$errorString);
    } #if
  } # if $bStreamSocket

  # send to the socket
  if( $bPipe )
  {
    $retVal = $socket->print( $payload );
  } # if
  else
  {
    $retVal = $socket->send( $payload );
  } # else
  if( !$retVal )
  {
    $errorString = "TxProc::serialise unix domain send failed - path:'$unixdomainPath' - $!";
    close($socket) if( $bLocalSocketCreated );
    return ($retVal,$errorString);
  } # if

  # read success or failure on submission and if we should wait for a response object
  # $retVal is 0 for an error, 1 for no response and 2 for a response
  if( $bStreamSocket )
  {
    ($retVal,$errorString) = $this->readReply( $socket );
  } # if $bStreamSocket

  # cleanup if required
  if( $bLocalSocketCreated )
  {
    close( $socket );
    $socket = 0;
  } # if

  return($retVal,$errorString);
} # serialise

1;  # module return value
