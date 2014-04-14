#!/usr/bin/perl -w
# $Id: txProcLogGrep.pl 3 2009-08-19 14:26:33Z gerhardus $
#
# Greps txProc's dispatcher log
#
# Gerhardus Muller
#

use strict;
use Getopt::Std;


# parse the command line options
our $logDir = "/var/log/txProc";
our $notifierLog = "$logDir/dispatcher.log";
our $beVerbose = 0;
our $bSipMessagesOnly = 0;
my %option = ();
getopts( "hvsl:", \%option );
# -v is verbose
$beVerbose = 1 if( exists( $option{v} ) );
# -h asks for help
if( ( exists( $option{h} ) ) || (@ARGV < 1) )
{
  print( "$0 options searchTerm\n" );
  print( "\t-l logfile\n" );
  print( "\t-s for the execution results only\n" );
  print( "\t-v for verbose output\n" );
  print( "\t-h for this help screen\n" );
  exit( 1 );
} # help

$notifierLog = $option{l} if( exists( $option{l} ) );
$bSipMessagesOnly = 1 if( exists( $option{s} ) );

my $searchTerm = $ARGV[0];
print "Retrieve log lines related to '$searchTerm in $notifierLog\n" if( $beVerbose );

die( "Failed to open logfile '$notifierLog'" ) if( !open( LOGFILE, "<", $notifierLog ) );

# run through the log the first time and gather all the log ids that are relevant
my $lineNo = 0;
my %logKeys;
my $lastRef;
while( <LOGFILE> )
{
  my $line = $_;
  $lineNo++;
  $lastRef = $1 if( $line =~ /^\[(\d+-\d+-\d+ \d+:\d+:\d+ \d+)/ );
  $logKeys{$lastRef} = $lineNo if( ($line =~ /$searchTerm/) && defined($lastRef) && !exists($logKeys{$lastRef}) );
} # while

if( $beVerbose )
{
  print "List of references and the first line encountered\n";
  foreach my $key (keys(%logKeys))
  {
    print( "$key - line $logKeys{$key}\n" );
  } # foreach
} # if

# retrieve the corresponding lines
print "\nList of references and corresponding log entries\n\n";
foreach my $key (sort keys(%logKeys))
{
  my $logHeader = '';
  $logHeader .= "#########-------------#########\n" if( !$bSipMessagesOnly );
  $logHeader .=  "##" if( $bSipMessagesOnly );
  $logHeader .=  " $key\n";
  $logHeader .=  "#########-------------#########\n" if( !$bSipMessagesOnly );
  my $logText = extractLogEntries( $key );
  if( length($logText) > 1 )
  {
    print $logHeader;
    print "$logText\n\n";
  } # if
} # foreach

close LOGFILE;

sub extractLogEntries
{
  my ($searchKey) = @_;
  my $searchKeyLen = length( $searchKey );
  seek( LOGFILE, 0, 0 );

  my $lineNo = 0;
  my $lastLine = '';
  my $logText = '';
  my $bMoreLines = 0;
  while( <LOGFILE> )
  {
    my $line = $_;
    chomp $line;
    $lineNo++;
    if( $line =~ /^\[$searchKey/ )
    {
      $lastLine = "[ ".substr( $line, $searchKeyLen+2 )."\n";
      $logText .= $lastLine if( !$bSipMessagesOnly );
      $bMoreLines = 1;
    } # if
    else
    {
      # capture all the text following a valid line until the next valid log entry
      $bMoreLines = 0 if( $line =~ /^\[(\d+-\d+-\d+ \d+:\d+:\d+ \d+)/ );
      if( $bMoreLines )
      {
        # remove the ^M character at the end of the SIP messages
        chop $line if( ord(substr($line,-1,1)) == 13 );
        $logText .= $lastLine if( $bSipMessagesOnly && (length($lastLine)>0) );
        $logText .= "$line\n" if( $bMoreLines && (length($line)>0) );
        $lastLine = '';
      } # if
    } # else
  } # while
  return $logText;
} # extractLogEntries
