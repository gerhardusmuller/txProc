#!/usr/bin/perl -w
# Deploys txProc to a remote server
# Requires sudo rights on the account that runs the script on, it should also be part of the $groupOwner group
#
# perl -MCPAN -e "install File::Basename"
#
# $Id: deployTxProc.pl 2944 2013-11-20 16:13:08Z gerhardus $
# Versioning: a.b.c a is a major release, b represents changes or new features, c represents bug fixes. 
# @version 1.0.0		18/07/2013		Gerhardus Muller		Script created
# @version 1.1.0		12/06/2014		Gerhardus Muller		support for ubuntu / changed the default perl modules / conf to be /usr/local/etc; modules /usr/local/lib/site_perl
# @version 1.2.0		17/06/2014		Gerhardus Muller		ability to clone a perl installation
#
# Gerhardus Muller
#

use strict;
use Getopt::Std;
use File::Basename;

our $runAsUser = 'uucp';
our $groupOwner = 'uucp';
our $curlppSrc = 'curlpp-0.7.3/src/curlpp/.libs/';
our $curlpp1So = 'libcurlpp.so.0.0.2';
our $txProcSrc = 'go/src/github.com/gerhardusmuller/txProc';

our $execDir;
our $logDir;
our $etcDir;
our $etcLocalDir;
our $perlModulesDir;
our $dailyCronDir;

# remote executable locations and options
our $remoteDistro;
our $remoteLibDir;
our $perlSysDir;
our $bInstallLibCurlpp;
our $jsonLib;
our $install;
our $ln;
our $rm;
our $cp;
our $sh;
our $sudo;
our $ldconfig;

# parse the command line options
my %option = ();
getopts( "abc:e:ghl:no:O:pPs:t", \%option );
our $bNewInstallation = exists($option{n})?1:0;
our $bCleanup = exists($option{b})?1:0;
our $containerName = exists($option{c})?$option{c}:undef;
our $persistentExec = exists($option{e})?$option{e}:undef;
our $libFile = exists($option{s})?$option{s}:undef;
our $compiledLibRoot = exists($option{l})?$option{l}:$ENV{HOME}.'/src';
our $confToInstall = exists($option{o})?$option{o}:undef;
our $confToLink = exists($option{O})?$option{O}:undef;
our $bInstallPerlModules = exists($option{p})?1:0;
our $bSymlinkPerlModules = exists($option{P})?1:0;
our $bInstallAdminScripts = exists($option{a})?1:0;
our $bLegacyPerlModulesLocation = exists($option{g})?1:0;
our $bClonePerl = exists($option{t})?1:0;
if( exists($option{h}) || (@ARGV < 1) )
{
  print( "$0 options destination\n" );
  print( "  -n new installation or upgrading executables\n" );
  print( "  -b cleanup installation files\n" );
  print( "  -c container - install txProc inside a container on destination\n" );
  print( "  -g use legacy perl module location rather than /usr/local/lib/site_perl\n" );
  print( "  -l compiledLibRoot - root folder for json-cpp and curlpp ($compiledLibRoot)\n" );
  print( "  -o conf file to install (local file) - txProc conf only\n" );
  print( "  -O target conf file to symlink to (file on target) - txProc conf only\n" );
  print( "  -p install perl modules\n" );
  print( "  -P sym link perl modules\n" );
  print( "  -a install the admin scripts\n" );
  print( "  -e executable - install a txProc persistent executable rather than txProc\n" );
  print( "  -s so or lib - install a third party library into /usr/local/lib64 irrespective where it originates from - install for ex libspandsp.so.2.0.0 - the process will create the symlinks\n" );
  print( "  -t clone the perl installation to the remote node\n" );
  print( "run the script from the vts directory\n" );
  exit( 1 );
} # if

our $target = $ARGV[0];
detectDistro();

if( defined($persistentExec) )
{
  deployPersistentExec();
} # if
elsif( defined($libFile) )
{
  deployDll( $libFile );
} # if
elsif( $bClonePerl )
{
  clonePerl();
} # if
else
{
  deployTxProc();
} # else
exit(0);

######
sub deployPersistentExec
{
  # assume this script is run from the vts directory
  generatePathNames();
  print "======\ndeploying $persistentExec to:$target\n";

  # generate the version name variants

  # install the executable - first strip the debug info
  print "installing the executable $persistentExec\n";
  my ($fileName,$filePath,$fileExtension) = fileparse( $persistentExec, qr{\..+?} );
  `install -s $persistentExec /tmp/`;
  installAsRoot( "/tmp/$fileName$fileExtension", "$execDir/" );
} # deployPersistentExec

######
# this function is most likely going to need extra attention for versioned dlls
# we pull a nasty and always install in /usr/local/lib64 - both good and bad
sub deployDll
{
  my ($libFile) = @_;

  # assume this script is run from the vts directory
  # the destination directories depend on if it is a container installation or not
  generatePathNames();

  # install the library - first strip the debug info
  print "installing the lib/so $libFile\n";
  my ($fileName,$filePath,$fileExtension) = fileparse( $libFile, qr{\..+?} );
  my @versions = split '\.', $fileExtension;
  my @symlinks;
  my $mainFile = $fileName.$fileExtension;
  my $baseName = $fileName;
  my $symlinkPartScript;
  my $symlinkScript;
  for( my $i=1;($i<@versions-1)&&(@symlinks<2);$i++ )
  {
    my $version = $versions[$i];
    $baseName .= ".$version";
    push @symlinks,$baseName;
    $symlinkPartScript .= "$ln -s -f $mainFile $baseName; ";
  } # for
  $symlinkScript = "$sudo $sh -c \"(cd $remoteLibDir && { $symlinkPartScript })\"" if(@symlinks>0);
  print "filePath:$filePath main libName:$mainFile symlinkScript:".(defined($symlinkScript)?$symlinkScript:'')."\n";
  `install -s $libFile /tmp/`;
  installAsRoot( "/tmp/$fileName$fileExtension", "$remoteLibDir/" );
  execRemote( $symlinkScript ) if(defined($symlinkScript));
  runLdConfig();
} # deployDll

######
sub deployTxProc
{
  # assume this script is run from the vts directory
  # the destination directories depend on if it is a container installation or not
  generatePathNames();
  print "======\ndeploying txProc to:$target ".($bNewInstallation?'new':'existing')." installation\n";

  # create directories
  if( $bNewInstallation )
  {
    print "creating log directory $logDir\n";
    execRemote( "$sudo $install -g $groupOwner -o $runAsUser -m 'u=rwx,g=rwx' -d $logDir" );
    execRemote( "$sudo $install -g $groupOwner -o $runAsUser -m 'u=rwx,g=rwx' -d $logDir/recovery" );
    execRemote( "$sudo $install -g $groupOwner -o $runAsUser -m 'u=rwx,g=rwx' -d $logDir/stats" );
    execRemote( "$sudo $install -g $groupOwner -o $runAsUser -m 'u=rwx,g=rwx' -d $logDir/oldlogs" );

    print "creating etc/bin directory $etcLocalDir\n";
    execRemote( "$sudo $install -o root -m 'u=rwx,g=rwx,o=rx' -d $etcLocalDir" );
    execRemote( "$sudo $install -o root -m 'u=rwx,g=rwx,o=rx' -d $execDir" );
    execRemote( "$sudo $install -o root -m 'u=rwx,g=rwx,o=rx' -d $remoteLibDir" );

    if( $bInstallPerlModules || $bSymlinkPerlModules )
    {
      print "creating the perl modules directory $perlModulesDir\n";
      execRemote( "$sudo $install -g root -o root -m 'u=rwx,g=rwx,o=rx' -d $perlModulesDir " );
    } # if
  } # if

  # install system scripts
  if( $bNewInstallation )
  {
    print "installing the init.d script\n";
    installAsRoot( $txProcSrc.'/cpp/scripts/txProc.init_d', "$etcDir/init.d/txProc" );

    print "installing the logrotate.d script\n";
    installAsRoot( 'system/perl/logArchive.pl', "$execDir/" );
    installAsRoot( $txProcSrc.'/cpp/scripts/txProc.logrotate_d', "$etcDir/logrotate.d/txProc" );

    print "installing cleanup script\n";
    installAsRoot( $txProcSrc.'/cpp/scripts/txProcCleanup.sh', "$dailyCronDir/" );
  } # if

  # install the admin scripts 
  if( $bInstallAdminScripts )
  {
    print "installing the admin scripts\n";
    installAsRoot( $txProcSrc.'/cpp/scripts/txProcLogGrep.pl', "$execDir/" );
    installAsRoot( $txProcSrc.'/cpp/scripts/txProcRecover.pl', "$execDir/" );
    installAsRoot( $txProcSrc.'/cpp/scripts/txProcAdmin.pl', "$execDir/" );
    installAsRoot( $txProcSrc.'/cpp/scripts/txProcQueueLen.sh', "$execDir/" );
  } # if
  # install the modules
  if( $bInstallPerlModules )
  {
    installAsRoot( $txProcSrc.'/perl/Log.pm', "$perlModulesDir/" );
    installAsRoot( $txProcSrc.'/perl/TxProc.pm', "$perlModulesDir/" );
    installAsRoot( $txProcSrc.'/perl/Utils.pm', "$perlModulesDir/" );
    installAsRoot( $txProcSrc.'/perl/AppBase.pm', "$perlModulesDir/" );
    installAsRoot( $txProcSrc.'/perl/TxProcRecover.pm', "$perlModulesDir/" );
    installAsRoot( $txProcSrc.'/perl/TxProcServerCommand.pm', "$perlModulesDir/" );
  } # if

  # install compiled libraries
  if( $bNewInstallation )
  {
    print "installing compiled libraries\n";
    installAsRoot( "$compiledLibRoot/$jsonLib", "$remoteLibDir/libjson_linux.so" );
    if( $bInstallLibCurlpp )
    {
      $libFile = "$compiledLibRoot/$curlppSrc/$curlpp1So";
      deployDll( $libFile );
    } # if
  } # if

  # create the /etc/services entries
  if( $bNewInstallation )
  {
    print "making $etcDir/services entries\n";
    my $txprocServices = "/tmp/txproc.services";
    open(DESC, ">", $txprocServices) or die "ERROR failed to open $txprocServices - $!\n";
    print DESC "txproc         271/tcp\n";
    print DESC "txproc         271/udp\n";
    close DESC;

    my $tmpCmd = "/tmp/etcServices.sh";
    my $searchTerm = ($remoteDistro eq 'SLES')?'http-mgmt\s*280':'pawserv\s*345';
    open(DESC, ">", $tmpCmd) or die "ERROR failed to open $tmpCmd - $!\n";
    print DESC "if grep -P '^txproc\\s+271' $etcDir/services\n";
    print DESC "then echo ==services exists\n";
    print DESC "else cd /tmp\n";
    print DESC "  csplit -k $etcDir/services '/^$searchTerm/'\n";
    print DESC "  cat xx00 txproc.services xx01 > $etcDir/services\n";
    print DESC "fi";
    close DESC;
    my $cmd = "rsync $tmpCmd $txprocServices $target:/tmp/"; print "cmd - ".`$cmd`;
    execRemote( "$sudo $sh $tmpCmd" );
  } # if
  
  # run ldConfig
  runLdConfig() if($bNewInstallation);

  # install the executable - first strip the debug info
  if( $bNewInstallation )
  {
    print "installing the executable\n";
    `install -s $txProcSrc/cpp/bin/txProc /tmp/`;
    installAsRoot( '/tmp/txProc', "$execDir/" );
  } # if

  # install or link the conf
  if( defined($confToInstall) )
  {
    print "installing the conf file $confToInstall\n";
    installAsRoot( $confToInstall, "$etcLocalDir/txProc.cfg" );
    execRemote( "ls -l $etcLocalDir/txProc.cfg" );
  } # if
  if( defined($confToLink) )
  {
    print "linking the conf file $confToLink\n";
    execRemote( "$sudo $ln -s -f $confToLink $etcLocalDir/txProc.cfg; ls -l $etcLocalDir/txProc.cfg" );
  } # if
} # deployTxProc

######
sub clonePerl
{
  my @perlDirs = split /:/,$perlSysDir;
  foreach my $dir (@perlDirs)
  {
    if( $dir =~ /^(.+)\/(\w+)$/ )
    {
      my $firstPart = $1;
      my $lastPart = $2;
      my $cmd = "rsync -av $dir $target:/tmp/";
      my $remoteCmd = "$sudo $cp -dr --no-preserve=ownership /tmp/$lastPart $firstPart/; $rm -r /tmp/$lastPart";
      print "firstPart:$firstPart lastPart:$lastPart cmd:$cmd remoteCmd:$remoteCmd\n";
      print "cmd - ".`$cmd`;
      execRemote( $remoteCmd );
    } # if
    else
    {
      print "clonePerl WARN perlSysDir component:$dir could not be parsed!\n";
    } # if
  } # foreach
} # sub clonePerl

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
  $rem  = "$sudo $install -g root -o root /tmp/$fileName$fileExtension $scriptTarget";
  $rem .="; $rm /tmp/$fileName$fileExtension" if($bCleanup);
  execRemote( $rem );
} # installAsRoot

######
sub execRemote
{
  my $cmd = shift;
  my $exec = "ssh $target '$cmd'";
  my $result = `$exec`;
  chomp $result;
  print "$exec : $result\n";
  return $result;
} # execRemote

######
sub generatePathNames
{
  $logDir = "/var/log/txProc";
  $execDir = "/usr/local/bin";
  $etcLocalDir = "/usr/local/etc";
  $etcDir = "/etc";
  if( $bLegacyPerlModulesLocation )
  {
    $perlModulesDir = "/home/vts/vts/perl/modules";
  } # if
  else
  {
    $perlModulesDir = "/usr/local/lib/site_perl";
  } # else
  $dailyCronDir = "/etc/cron.daily";
  if( defined($containerName) )
  {
    my $containerRoot = "/var/lib/lxc/$containerName/rootfs";
    $logDir = "/var/log/$containerName/txProc";
    $execDir = "$containerRoot$execDir";
    $remoteLibDir = "$containerRoot$remoteLibDir";
    $etcLocalDir = "$containerRoot$etcLocalDir";
    $etcDir = "$containerRoot$etcDir";
    $perlModulesDir = "$containerRoot$perlModulesDir";
    $dailyCronDir = "$containerRoot$dailyCronDir";
  } # if
} # generatePathNames

######
sub runLdConfig
{
  if( defined($containerName) )
  {
    execRemote( "$sudo $ldconfig -r /var/lib/lxc/$containerName/rootfs" );
  } # if
  else
  {
    execRemote( "$sudo $ldconfig" );
  } # else
} # runLdConfig

######
sub detectDistro
{
  my $result = execRemote( "cat /etc/issue" );
  if( $result =~ /SUSE Linux Enterprise Server/ )
  {
    $remoteDistro = 'SLES';
  } # if
  elsif( $result =~ /Ubuntu/ )
  {
    $remoteDistro = 'Ubuntu';
  } # if
  else
  {
    die "detectDistro unable to identify remote distro:'$result'\n";
  } # else

  print "remote distro:$remoteDistro\n";
  if( $remoteDistro eq 'SLES' )
  {
    $remoteLibDir = "/usr/local/lib64";
    $perlSysDir = "/usr/lib/perl5";
    $jsonLib = 'jsoncpp-src-0.5.0/libs/linux-gcc-4.3/libjson_linux-gcc-4.3_libmt.so';
    $bInstallLibCurlpp = 1;
    $install = '/usr/bin/install';
    $ln = '/bin/ln';
    $rm = '/bin/rm';
    $cp = '/bin/cp';
    $sh = '/usr/bin/sh';
    $sudo = '/usr/bin/sudo';
    $ldconfig = '/sbin/ldconfig';
  } # if
  elsif( $remoteDistro eq 'Ubuntu' )
  {
    $remoteLibDir = "/usr/local/lib";
    $perlSysDir = "/usr/lib/perl5:/usr/local/lib/perl:/usr/local/share/perl";
    $jsonLib = 'jsoncpp-src-0.5.0/libs/linux-gcc-4.8/libjson_linux-gcc-4.8_libmt.so';
    $bInstallLibCurlpp = 0;
    $install = '/usr/bin/install';
    $ln = '/bin/ln';
    $rm = '/bin/rm';
    $cp = '/bin/cp';
    $sh = '/bin/sh';
    $sudo = '/usr/bin/sudo';
    $ldconfig = '/sbin/ldconfig';
  } # else
  else
  {
    die "detectDistro remoteDistro:$remoteDistro not supported\n";
  } # else
} # detectDistro
