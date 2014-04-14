#!/usr/bin/perl -w
# $Id: nucleusLogGrep.pl 2629 2012-10-19 16:52:17Z gerhardus $
#
# Greps dispatcher log for entries
#
# Gerhardus Muller 
#

use strict;
use Getopt::Std;

# parse the command line options
our $logDir = "/var/log/txProc";
our $nucleusLog = "$logDir/nucleus.log";
our $beVerbose = 0;
our $bResultMessagesOnly = 0;
my %option = ();
getopts( "hvrl:", \%option );
# -v is verbose
$beVerbose = 1 if( exists( $option{v} ) );
# -h asks for help
if( ( exists( $option{h} ) ) || (@ARGV < 1) )
{
  print( "$0 options searchTerm\n" );
  print( "\t-l logfile\n" );
  print( "\t-r for the result messages only\n" );
  print( "\t-v for verbose output\n" );
  print( "\t-h for this help screen\n" );
  exit( 1 );
} # help

$nucleusLog = $option{l} if( exists( $option{l} ) );
$bResultMessagesOnly = 1 if( exists( $option{r} ) );

my $searchTerm = $ARGV[0];
print "Retrieve log lines related to '$searchTerm in $nucleusLog\n" if( $beVerbose );

die( "Failed to open logfile '$nucleusLog'" ) if( !open( LOGFILE, "<", $nucleusLog ) );

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
  $logHeader .= "\n#########-------------#########\n" if( !$bResultMessagesOnly );
  $logHeader .=  "##" if( $bResultMessagesOnly );
  $logHeader .=  " $key\n";
  $logHeader .=  "#########-------------#########\n" if( !$bResultMessagesOnly );
  my $logText = extractLogEntries( $key );
  if( length($logText) > 1 )
  {
    print $logHeader;
    print "$logText\n";
  } # if
} # foreach

close LOGFILE;

sub extractLogEntries
{
  my ($searchKey) = @_;
  my $searchKeyLen = length( $searchKey );
  seek( LOGFILE, 0, 0 );

  my $lineNo = 0;
  my $line;
  my $logText = '';
  my $bMoreLines = 0;
  my $bCaptureLastValidLine = 0;
  while( <LOGFILE> )
  {
    my $lastLine = $line;
    $line = $_;
    chomp $line;
    $lineNo++;
    if( $line =~ /^\[$searchKey/ )
    {
      $logText .= "[".substr( $line, $searchKeyLen+2 )."\n" if( !$bResultMessagesOnly );
      $bMoreLines = 1;
      $bCaptureLastValidLine = 1;
    } # if
    else
    {
      # capture all the text following a valid line, the last valid line 
      # until the next valid log entry
      $bMoreLines = 0 if( $line =~ /^\[(\d+-\d+-\d+ \d+:\d+:\d+ \d+)/ );
      if( $bMoreLines )
      {
        if( $bCaptureLastValidLine && $bResultMessagesOnly )
        {
          $logText .= "[".substr( $lastLine, $searchKeyLen+2 )."\n";
          $bCaptureLastValidLine = 0;
        } # if
        $logText .= "$line\n";
      } # if
    } # else
  } # while
  return $logText;
} # extractLogEntries
