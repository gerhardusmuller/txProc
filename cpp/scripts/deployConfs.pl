#!/usr/bin/perl -w
# Deploys conf files to remote hosts
# Perl confs always land up in vts/perl/conf
# txProc confs always land up in /usr/local/etc/
# Requires sudo rights on the account that runs the script on, it should also be part of the $groupOwner group
#
# perl -MCPAN -e "install File::Basename"
#
# $Id: deployConfs.pl 2905 2013-09-11 07:36:42Z gerhardus $
# Versioning: a.b.c a is a major release, b represents changes or new features, c represents bug fixes. 
# @version 1.0.0		25/07/2013		Gerhardus Muller		Script created
# @version 1.1.0		13/07/2014		Gerhardus Muller		switched default perl conf location to /usr/local/etc
# @version 1.2.0		17/07/2014		Gerhardus Muller		deployTxProcConf and deployPerlConfs do virtually the same thing
#
# Gerhardus Muller
#

use strict;
use Getopt::Std;
use File::Basename;

our $runAsUser = 'uucp';
our $groupOwner = 'uucp';

# parse the command line options
my %option = ();
getopts( "bc:ehpPt", \%option );
our $bCleanup = exists($option{b})?1:0;
our $containerName = exists($option{c})?$option{c}:undef;
our $bDeployExim = exists($option{e})?1:0;
our $bDeployPerl = (exists($option{p})||exists($option{P}))?1:0;
our $bLegacyPerlConfs = exists($option{P})?1:0;
our $bDeployTxProc = exists($option{t})?1:0;
if( exists($option{h}) || (@ARGV < 2) )
{
  print( "$0 options destination conf file(s)\n" );
  print( "  -b cleanup installation files\n" );
  print( "  -c container - install txProc inside a container on destination\n" );
  print( "  -e deploy exim confs van deploy exim.filter and exim.conf simultaneously\n" );
  print( "  -p deploy perl confs - to /usr/local/etc\n" );
  print( "  -P deploy perl confs - to the legacy location\n" );
  print( "  -t deploy txProc confs or persistent process confs\n" );
  print( "run the script from the vts directory\n" );
  exit( 1 );
} # if

our $target = $ARGV[0];

deployEximConfs() if($bDeployExim);
deployPerlConfs() if($bDeployPerl);
deployTxProcConf() if($bDeployTxProc);
exit(0);

######
sub deployTxProcConf
{
  # assume this script is run from the vts directory
  # the destination directories depend on if it is a container installation or not
  my $etcLocalDir;

  $etcLocalDir = "/usr/local/etc";
  if( defined($containerName) )
  {
    my $containerRoot = "/var/lib/lxc/$containerName/rootfs";
    $etcLocalDir = "$containerRoot$etcLocalDir";
  } # if

  my $confs = '';
  for( my $i=1; $i<@ARGV; $i++ )
  {
    my $file = $ARGV[$i];
    $confs .= "$file ";
    print "installing file $file\n";
    installAsRoot( $file, "$etcLocalDir/" );
  } # for
  print "installed the conf files $confs to $etcLocalDir\n";
} # deployTxProcConf

######
sub deployPerlConfs
{
  # assume this script is run from the vts directory
  # the destination directories depend on if it is a container installation or not
  my $perlConfDir;

  $perlConfDir = $bLegacyPerlConfs?"/home/vts/vts/perl/conf":"/usr/local/etc";
  if( defined($containerName) )
  {
    my $containerRoot = "/var/lib/lxc/$containerName/rootfs";
    $perlConfDir = "$containerRoot$perlConfDir";
  } # if

  my $confs = '';
  for( my $i=1; $i<@ARGV; $i++ )
  {
    my $file = $ARGV[$i];
    $confs .= "$file ";
    print "installing file $file\n";
    installAsRoot( $file, "$perlConfDir/" );
  } # for
  print "installed the perl conf files $confs to $perlConfDir\n";
} # deployPerlConfs

######
sub deployEximConfs
{
  # assume this script is run from the vts directory
  # the destination directories depend on if it is a container installation or not
  my $eximConfDir;

  $eximConfDir = "/etc";
  if( defined($containerName) )
  {
    my $containerRoot = "/var/lib/lxc/$containerName/rootfs";
    $eximConfDir = "$containerRoot$eximConfDir";
  } # if

  my $confs = '';
  for( my $i=1; $i<@ARGV; $i++ )
  {
    my $file = $ARGV[$i];
    $confs .= "$file ";
    print "installing file $file\n";
    installAsRoot( $file, "$eximConfDir/" );
  } # for
  execRemote( 'sudo kill -HUP `cat /var/run/exim.pid`', $containerName );
  print "installed the exim conf files $confs\n";
} # deployEximConfs

######
sub installAsRoot
{
  my ($script,$scriptTarget) = @_;
  my $cmd;
  my $rem;

  my $scriptSize = -s $script;
  if( !defined($scriptSize) )
  {
    print "installAsRoot WARN script:$script scriptTarget:$scriptTarget does not exist\n";
    return;
  } # if

  my ($fileName,$filePath,$fileExtension) = fileparse( $script, qr{\..+?} );
  my $opt = ($scriptSize > 100000)?'-P':'';
  $cmd = "rsync $opt $script $target:/tmp/$fileName$fileExtension"; print "$cmd : ".`$cmd`;
  $rem  = "sudo /usr/bin/install -g root -o root /tmp/$fileName$fileExtension $scriptTarget";
  $rem .="; rm /tmp/$fileName$fileExtension" if($bCleanup);
  execRemote( $rem );
} # installAsRoot

sub execRemote
{
  my ($cmd,$machine) = @_;
  $machine = $target if(!defined($machine));
  my $exec = "ssh $machine '$cmd'";
  my $result = `$exec`;
  chomp $result;
  print "$exec : $result\n";
} # execRemote

