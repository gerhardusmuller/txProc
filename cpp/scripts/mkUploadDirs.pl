#!/usr/bin/perl -w
# $Id: mkUploadDirs.pl 2629 2012-10-19 16:52:17Z gerhardus $
#
# Script to create the temp upload directory structure remoteUpload mserver module
# Should be run as root
#
# Gerhardus Muller
#

use strict;
use Getopt::Std;

my $baseDir = "/data/upload/";
my $dirOwner = 'uucp';
my $dirGroup = 'uucp';
my $chmodFlags = 'u=rwx,g=rwx';

# make sure the baseDir exists
my $cmd = "install -g $dirGroup -o $dirOwner -m $chmodFlags -d $baseDir";
print "$cmd: " . `$cmd`;

# create the range A-Z
#for( my $i = ord('A'); $i <= ord('Z'); $i++ )
#{
#  my $cmd = "install -g $dirGroup -o $dirOwner -m $chmodFlags -d $baseDir".chr($i);
#  print "$cmd: " . `$cmd`;
#} # for 

# create the range a-z
for( my $i = ord('a'); $i <= ord('z'); $i++ )
{
  my $cmd = "install -g $dirGroup -o $dirOwner -m $chmodFlags -d $baseDir".chr($i);
  print "$cmd: " . `$cmd` . "\n";
} # for 

