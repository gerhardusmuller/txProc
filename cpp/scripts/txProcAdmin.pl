#!/usr/bin/perl -w
# $Id: txProcAdmin.pl 2786 2013-02-07 17:41:40Z gerhardus $
#
# Dynamic config guide for txProc
#
# Gerhardus Muller
#
use strict;
use Getopt::Std;
use Time::localtime;
use lib '/home/vts/vts/perl/modules';
use TxProcServerCommand;

#my $fuplTxProcName = '';
my $fuplTxProcService = 'txproc';
my $fuplTxProcName;
my $fuplTxProcUnixPath = '/var/log/txProc/txProc.sock';

# parse the command line options
our $beVerbose = 0;
my  %option = ();
getopts( "hvs:p:u:", \%option );
# -v is verbose
$beVerbose = 1 if( exists( $option{v} ) );
# -h asks for help
if( exists( $option{h} ) || (@ARGV < 1) )
{
  print( "$0 options command command options\n" );
  print( "  -s server name if TCP is used\n" );
  print( "  -p server service or port (default '$fuplTxProcService')\n" );
  print( "  -u Unix socket path (default to use - defaults to '$fuplTxProcUnixPath')\n" );
  print( "  -v for verbose output and listing of selected events\n" );
  print( "  -h for this help screen\n" );
  print( "Commands:\n" );
  print( "  shutdown\n" );
  print( "  reopenlog\n" );
  print( "  stats\n" );
  print( "  maxqueuelen queue maxval\n" );
  print( "  maxexectime queue maxexec\n" );
  print( "  numworkers queue numworkers\n" );
  print( "  freeze/unfreeze queue\n" );
  print( "  loglevel main|nucleus|networkIf value (1-10 10 log everything)\n" );
  print( "  createqueue queue - queue has to be present in the config file - it will be re-read\n" );
  print( "  deletequeue queue - use the nucleus.XXX.name value; freeze and set the numworkers to 0 beforehand\n" );
  print( "  submitpacket filename - submits txProc serialised packet for execution\n" );
  exit( 1 );
} # help

if( exists($option{s}) )
{
  $fuplTxProcName = $option{s};
  undef $fuplTxProcUnixPath;
} # if
$fuplTxProcService = $option{p} if( exists( $option{p} ) );
$fuplTxProcUnixPath = $option{u} if( exists( $option{u} ) );
print "server connect: ";
print "unix domain '$fuplTxProcUnixPath' " if( defined($fuplTxProcUnixPath) );
print "tcp server '$fuplTxProcName' " if( defined($fuplTxProcName) );
print ", tcp service '$fuplTxProcService' " if( defined($fuplTxProcService) );
print "\n";

my $cmd = $ARGV[0];
my $txProcCmd = new TxProcServerCommand( $fuplTxProcName,$fuplTxProcService,$fuplTxProcUnixPath );
my $retVal;
my $submitErrorString;

if( $cmd eq 'shutdown' )
{
  print "Executing command '$cmd'\n";
  ($retVal,$submitErrorString) = $txProcCmd->shutdown();
  die "ERROR failed to submit to txProc: '$submitErrorString'\n" if( !$retVal );
} # if shutdown
elsif( $cmd eq 'reopenlog' )
{
  print "Executing command '$cmd'\n";
  ($retVal,$submitErrorString) = $txProcCmd->reopenLog();
  die "ERROR failed to submit to txProc: '$submitErrorString'\n" if( !$retVal );
} # reopenlog
elsif( $cmd eq 'loglevel' )
{
  die "command '$cmd' requires parameters 'main|nucleus|networkIf' 'val'" if(@ARGV < 3);
  my $target = $ARGV[1];
  my $val = $ARGV[2];
  print "Executing command '$cmd', target '$target', val $val\n";
  if( $target eq 'main' )
  {
    ($retVal,$submitErrorString) = $txProcCmd->setMainLogLevel( $val );
  } # if
  elsif( $target eq 'nucleus' )
  {
    ($retVal,$submitErrorString) = $txProcCmd->setNucleusLogLevel( $val );
  } # elsif
  elsif( $target eq 'networkIf' )
  {
    ($retVal,$submitErrorString) = $txProcCmd->setNetworkIfLogLevel( $val );
  } # elsif
  else
  {
    die "command '$cmd' does not support target '$target'";
  } # else
  die "ERROR failed to submit to txProc: '$submitErrorString'\n" if( !$retVal );
} # if loglevel
elsif( ( $cmd eq 'maxqueuelen' ) || ( $cmd eq 'maxexectime' ) || ( $cmd eq 'numworkers' ) )
{
  die "command '$cmd' requires parameters 'queue' 'val'" if(@ARGV < 3);
  my $queue = $ARGV[1];
  my $val = $ARGV[2];
  print "Executing command '$cmd' queue '$queue' value '$val'\n";
  if( $cmd eq 'maxqueuelen' )
  {
    ($retVal,$submitErrorString) = $txProcCmd->setMaxQueueLen( $queue, $val );
  }
  elsif( $cmd eq 'maxexectime' )
  {
    ($retVal,$submitErrorString) = $txProcCmd->setMaxExecTime( $queue, $val );
  }
  elsif( $cmd eq 'numworkers' )
  {
    ($retVal,$submitErrorString) = $txProcCmd->setNumWorkers( $queue, $val );
  }
  die "ERROR failed to submit to txProc: '$submitErrorString'\n" if( !$retVal );
} # queue adjust
elsif( ( $cmd eq 'freeze' ) || ( $cmd eq 'unfreeze' ) )
{
  die "command '$cmd' requires parameters 'queue'" if(@ARGV < 2);
  my $queue = $ARGV[1];
  print "Executing command '$cmd' queue '$queue'\n";
  if( $cmd eq 'freeze' )
  {
    ($retVal,$submitErrorString) = $txProcCmd->freeze( $queue, "1" );
  }
  elsif( $cmd eq 'unfreeze' )
  {
    ($retVal,$submitErrorString) = $txProcCmd->freeze( $queue, "0" );
  }
  die "ERROR failed to submit to txProc: '$submitErrorString'\n" if( !$retVal );
} # queue adjust
elsif( $cmd eq 'createqueue' )
{
  die "command '$cmd' requires parameters 'queue'" if(@ARGV < 2);
  my $queue = $ARGV[1];
  print "Executing command '$cmd' queue '$queue'\n";
  ($retVal,$submitErrorString) = $txProcCmd->createQueue( $queue, 1 );
  die "ERROR failed to submit to txProc: '$submitErrorString'\n" if( !$retVal );
} # queue adjust
elsif( $cmd eq 'deletequeue' )
{
  die "command '$cmd' requires parameters 'queue'" if(@ARGV < 2);
  my $queue = $ARGV[1];
  print "Executing command '$cmd' queue '$queue'\n";
  ($retVal,$submitErrorString) = $txProcCmd->createQueue( $queue, 0 );
  die "ERROR failed to submit to txProc: '$submitErrorString'\n" if( !$retVal );
} # queue adjust
elsif( $cmd eq 'submitpacket' )
{
  die "command '$cmd' requires parameters 'filename'" if(@ARGV < 2);
  my $packetname = $ARGV[1];
  print "Executing command '$cmd' filename '$packetname'\n";
  my $bSuccess = execEvent( $packetname,$fuplTxProcUnixPath,$fuplTxProcName,$fuplTxProcService );
  die "ERROR failed to submit to txProc\n" if( !$bSuccess );
} # queue adjust
elsif( $cmd eq 'stats' )
{
  print "Executing command '$cmd'\n";
  ($retVal,$submitErrorString) = $txProcCmd->stats();
} # stats
else
{
  die "did not recognise command '$cmd'";
} # else


######
# executes a file based event
sub execEvent
{
  my ($eventFilename,$fuplTxProcUnixPath,$fuplTxProcName,$fuplTxProcService) = @_;
  my $bSuccess = 1;
  my $objToSubmit = '';

  if( open( OBJ, "< $eventFilename" ) )
  {
    my $oldSlurp = $/;
    undef $/; # enter slurp mode
    $objToSubmit = <OBJ>;
    $/ = $oldSlurp;
    close OBJ;
  } # if
  else
  {
    my $fatalReason = "unable to open object input file '$eventFilename'";
    print "ERROR execEvent:$fatalReason\n";
    return 0;
  }  # else

  # we are now supposed to have a serialised object
  # verify validity of the object
  my ($recoveryEvent,$parseError) = TxProc->unSerialiseFromString( $objToSubmit );
  if( !$recoveryEvent )
  {
    my $fatalReason = "illegal object $parseError";
    print "ERROR execEvent:$fatalReason\n";
    return 0;
  } # if

  my ($retVal,$submitErrorString) = $recoveryEvent->serialise( undef,$fuplTxProcUnixPath,$fuplTxProcName,$fuplTxProcService );
  if( !$retVal )
  {
    print "ERROR execEvent:failed to submit to txProc: '$submitErrorString'\n";
    return 0;
  } # if

  return $bSuccess;
} # execEvent

