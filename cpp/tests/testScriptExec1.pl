#!/usr/bin/perl -w
# $Id: testScriptExec1.pl 1 2009-08-17 12:36:47Z gerhardus $
#
# Basic test script for the dispatcher
# Can sleep to simulate slow running scripts
#
# Gerhardus Muller
#

use strict;
use Getopt::Std;

our $beVerbose = 0;
# parse the command line options
my %option = ();
getopts( "hvs:r:", \%option );
# -v is verbose
$beVerbose = 1 if( exists( $option{v} ) );
# -h asks for help
if( exists( $option{h} ) || (@ARGV < 0) )
{
  print( "$0 options parameters\n" );
  print( "\t-v for verbose output\n" );
  print( "\t-h for this help screen\n" );
  print( "\t-r retcode\n" );
  print( "\t-s noOfSecondsToSleep\n" );
  exit( 0 );
} # help

my $commandLine = "$0 \"" . join( '" "', @ARGV ) . "\"";

my $sleepTime = 0;
$sleepTime = $option{s} if( exists($option{s}) );
print "trace:$commandLine - sleeping ${sleepTime}s\n";

my $retCode = 0;
$retCode = $option{r} if( exists($option{r}) );

sleep( $sleepTime ) if( $sleepTime > 0 );

print "result:phpProcessedSuccess 1\n";
exit $retCode;

