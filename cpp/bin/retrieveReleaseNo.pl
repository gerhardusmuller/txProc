#!/usr/bin/perl -w
# $Id: retrieveReleaseNo.pl 1 2009-08-17 12:36:47Z gerhardus $

use strict;
use Shell qw( make strip );

my $svnCommand = "svn";
my $buildFile = "buildno.h";
my $currentDir = `pwd`;
chomp $currentDir;
my $startTime = scalar localtime( time() );

print "Current directory $currentDir - buildfile is $buildFile time is $startTime\n";

chdir "../";
#system( "svn update" );         # perform an update to retrieve the most recent version
my $svnOutput = `svn update`;   # retrieve the version number
my $rev = "unknown";
if( $svnOutput =~ /\srevision\s(\d*)\./ )
{
  $rev = $1;
}
else
{
  print "Failed to parse svn output:\n$svnOutput\n";
}
print "Current repository revision: $rev\n";

open BUILDFILE, ">$buildFile" or die "Cannot create $buildFile: $!";
print BUILDFILE "const char* buildno = \"$rev\";\n\n";
close BUILDFILE;

chdir "$currentDir" or die "Cannot change to $currentDir: $!\n";

