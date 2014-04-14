#!/usr/bin/perl -w
#
# test code 

# $Id: test1.pl 283 2010-04-12 15:24:48Z gerhardus $
# Versioning: a.b.c a is a major release, b represents changes or new features, c represents bug fixes. 
# @version 1.0.0		12/04/2010		Gerhardus Muller		Script created
#
#
# Gerhardus Muller
#

use strict;
use Getopt::Std;
use lib '/home/vts/vts/perl/modules';
use TxProc;

my $unixDomainSocket = '/var/log/txProc/txProc.sock';
my $testScriptName = '/home/gerhardus/projects/vts/cpp/txProc/tests/testTarget.pl';

for( my $i = 0; $i < 20; $i++ )
{
  my $event = new TxProc( 'EV_PERL' );
  $event->destQueue( 'default' );
  $event->scriptName( $testScriptName );
  $event->addScriptParam( "$i" );
  my ($retVal,$errString) = $event->serialise( undef, $unixDomainSocket, undef, undef );
  if( $retVal )
  {
    print "submitted event ".($i+1)."\n";
  } # if
  else
  {
    print "failed to submit: $errString\n";
  } # else
} # for
