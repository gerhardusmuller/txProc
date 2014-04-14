#!/usr/bin/perl -w
# $Id: updateBuildtime.pl 2812 2013-02-25 18:34:15Z gerhardus $

use strict;

my $buildFile = "../buildtime.h";
my $versionFile = "../buildno.h";
my $startTime = scalar localtime( time() );

open BUILDFILE, ">$buildFile" or die "Cannot create $buildFile: $!";
print BUILDFILE "const char* buildtime = \"$startTime\";\n";
close BUILDFILE;

my $svnOutput = `svnversion`;
if( defined($svnOutput) && ($svnOutput =~ /^(\d+)/) )
{
  my $rev = $1 ;
  open VERSIONFILE, ">$versionFile" or die "Cannot create $versionFile: $!";
  print VERSIONFILE "const char* buildno = \"$rev\";\n";
  close VERSIONFILE;
}
else
{
  print STDERR "not updating version - not available\n";
}
