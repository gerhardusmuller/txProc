#!/usr/bin/perl -w
# $Id: txProcRecover.pl 2911 2013-10-07 09:27:37Z gerhardus $
#
# Recovery script for txProc
#
# ./txProcRecover.pl -T1 -d -z -r -f ~/temp/proxy/recovery.log -P '[{"field":"destQueue","cmd":"regex","value":"/^remote/local/"}]'
# ./txProcRecover.pl -T1 -d -z -r -f ~/temp/proxy/recovery.log -P '[{"field":"num_pages","cmd":"regexparam","value":"/(.*)/5/"}]'
#
# Gerhardus Muller
#
use strict;
use Getopt::Std;
use Time::localtime;
use Time::HiRes qw( usleep gettimeofday tv_interval );
use JSON::XS;
use lib '/home/vts/vts/perl/modules';
use Log qw( openLog closeLog $timestamp $logYear $logMonth $logDay *LOGFILE );
use TxProcRecover qw( scanLog setReportsVebose );
use TxProc;

my $fuplTxProcService = 'txproc';
my $fuplTxProcName;
my $fuplTxProcUnixPath = '/var/log/txProc/txProc.sock';
my $recoveryLog = "/var/log/txProc/recovery.log";

# parse the command line options
our $beVerbose = 0;
my  %option = ();
getopts( "dhvSrRdlf:q:g:c:e:s:p:P:u:n:tT:z", \%option );
# -v is verbose
$beVerbose = 1 if( exists( $option{v} ) );
# -h asks for help
if( exists( $option{h} ) )
{
  print( "$0 options\n" );
  print( "  search criteria are combined with AND logic by default unless -l is specified\n" );
  print( "  -d dry run only - useful only for -r\n" );
  print( "  -f recovery log file (default '$recoveryLog')\n" );
  print( "  -r actually recover the selected events\n" );
  print( "  -R mark the selected events as ignored\n" );
  print( "  -q queue - select events on this queue only\n" );
  print( "  -g grepterm - on its own it displays records, with recovery it selects\n" );
  print( "  -c select only events of this class (EV_URL/EV_PERL ..)\n" );
  print( "  -e select only events of this error type (shutdown/exec_fail)\n" );
  print( "  -n select events of this status - unrecovered events are 'SUCC'\n" );
  print( "  -l change option combination logic to OR\n" );
  print( "  -s recovery server name if TCP is used\n" );
  print( "  -p recovery server service or port (default '$fuplTxProcService')\n" );
  print( "  -u Unix socket path (default to use - defaults to '$fuplTxProcUnixPath')\n" );
  print( "  -S do not summarise\n" );
  print( "  -d to list event detail\n" );
  print( "  -t count the number of recoverable events\n" );
  print( "  -T numEvents - when used with r/R limits processing to this number\n" );
  print( "  -z zero the retries counter of the events\n" );
  print( '  -P patch instructions - valid json string: [{"field":"name","cmd":"regex|regexparam|regexscript|dropparam|addparam|prependscript|deletescript|cat|catparam|catscript","value":"parameter to cmd" }'."\n");
  print( "    the *param commands all operate on named event parameters\n" );
  print( "    the *script commands all operate on event script parameters\n" );
  print( "    the cat* commands displays the value of the parameter on stdout (cat cats parameters such as url,scriptName,destQueue)\n" );
  print( "    regexparam operates on a param. in this case the field is the parameter name\n" );
  print( "    regexscript operates on scriptparams. in this case the field is the parameter index with 0 the first parameter\n" );
  print( "    for regex field has to resolve to a method name in the TxProc class for regex (examples are url,scriptName,destQueue)\n" );
  print( "    any regex based command should provide the regex as the value - an example would be '/replacethis/forthat/' including the forward slashes and modifiers\n" );
  print( "    ex: ./txProcRecover.pl -T1 -d -z -r -f ~/temp/proxy/recovery.log -P '[{\"field\":\"destQueue\",\"cmd\":\"regex\",\"value\":\"/^remote/local/\"}]'" );
  print( "    \n" );
  print( "  -v for verbose output and listing of selected events\n" );
  print( "  -h for this help screen\n" );
  exit( 1 );
} # help

my $bDryrun = 0;
$bDryrun = 1 if( exists( $option{d} ) );
my $bZeroRetries = 0;
$bZeroRetries = 1 if( exists( $option{z} ) );
my $bSummarise = 1;
$bSummarise = 0 if( exists( $option{S} ) );
my $grepTerm = '';
$grepTerm = $option{g} if( exists( $option{g} ) );
my $classSelect = '';
$classSelect = $option{c} if( exists( $option{c} ) );
my $errSelect = '';
$errSelect = $option{e} if( exists( $option{e} ) );
my $queueSelect = '';
$queueSelect = $option{q} if( exists( $option{q} ) );
my $statusSelect = '';
$statusSelect = $option{n} if( exists( $option{n} ) );
my $bCombineWithOr = 0;
$bCombineWithOr = 1 if( exists( $option{l} ) );
my $bCountRecoverableEvents = 0;
$bCountRecoverableEvents = 1 if( exists( $option{t} ) );
$recoveryLog = $option{f} if( exists( $option{f} ) );
my $bListDetail = 0;
if( exists($option{d}) )
{
  $bListDetail = 1;
  $beVerbose = 1;
} # if
my $bRecover = 0;
$bRecover = 1 if( exists( $option{r} ) );
my $bIgnore = 0;
$bIgnore = 1 if( exists( $option{R} ) );
die "cannot recover and ignore at the same time\n" if( $bRecover && $bIgnore );
my $limitNumEvents;
$limitNumEvents = $option{T} if( exists( $option{T} ) );
setReportsVebose(1) if( $beVerbose );
my $patchInstructions;
if( exists( $option{P} ) )
{
  my $patchInstructionsStr = $option{P};
  my $coder = JSON::XS->new->ascii->allow_nonref;
  $patchInstructions = $coder->decode( $patchInstructionsStr );
} # if

$statusSelect = 'SUCC' if( $bCountRecoverableEvents && (length($statusSelect)==0) );

# open log file - use stdout for interactive usage
my $logFile = 'stderr';
my $bLogOpen = 0;
(*LOGFILE, $timestamp, $bLogOpen) = openLog( $logFile );
die "ERROR failed to open the log stderr" if( !$bLogOpen );
print LOGFILE "\n$timestamp INFO application started\n" if(!$bCountRecoverableEvents);

# recovery connect parameters
if( exists($option{s}) )
{
  $fuplTxProcName = $option{s};
  undef $fuplTxProcUnixPath;
} # if
$fuplTxProcService = $option{p} if( exists( $option{p} ) );
$fuplTxProcUnixPath = $option{u} if( exists( $option{u} ) );

my $tmpLimit = '';
$tmpLimit = $limitNumEvents if( defined($limitNumEvents) );
my $recoveryTitle = "using recovery log file:'$recoveryLog' grep term:'$grepTerm', class select:'$classSelect', error select:'$errSelect' queue select:'$queueSelect' recovering:$bRecover ignoring:$bIgnore limit:$tmpLimit bDryrun$bDryrun";
print LOGFILE "$timestamp INFO $recoveryTitle\n" if(!$bCountRecoverableEvents);
my $prnUD = '';
my $prnIP = '';
$prnUD = $fuplTxProcUnixPath if( defined($fuplTxProcUnixPath) );
$prnIP = $fuplTxProcName if( defined($fuplTxProcName) );
print LOGFILE "$timestamp INFO recovery server unix domain '$prnUD', tcp server '$prnIP', tcp service '$fuplTxProcService'\n" if( $bRecover );
print LOGFILE "$timestamp INFO  Selected events:\n" if( $beVerbose );

my($bResult,$selectCount,$err) = scanLog( $bRecover,$bIgnore,$bSummarise,$bListDetail,$bCountRecoverableEvents,$bCombineWithOr,$recoveryLog,$recoveryTitle,$grepTerm,$classSelect,$queueSelect,$statusSelect,$errSelect,$limitNumEvents,$fuplTxProcUnixPath,$fuplTxProcName,$fuplTxProcService,$bDryrun,$bZeroRetries,$patchInstructions );
die "ERROR: $err\n" if( !$bResult );
print STDOUT $selectCount if( $bCountRecoverableEvents );
exit(0);

