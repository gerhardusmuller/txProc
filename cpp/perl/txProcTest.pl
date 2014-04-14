#!/usr/bin/perl -w

use strict;

use IO::Socket;
use lib '/home/vts/vts/perl/modules';
use TxProc;
use Log qw( generateTimestamp openLog closeLog $epochSecs $timeString $timestamp *LOGFILE );

my $udSocket = '/var/log/txProc/txProc.sock';
my $logFile = 'stderr';
my $bLogOpen = 0;

# open log file
(*LOGFILE, $timestamp, $bLogOpen) = openLog( $logFile );
die "ERROR failed to open the log" if( !$bLogOpen );
LOGFILE->autoflush(1);

my $event = new TxProc( 'EV_COMMAND' );
$event->command( 'CMD_PERSISTENT_APP' );
$event->destQueue( 'default' );
$event->beVerbose( 1 );
$event->addParam( 'param', 'http://php2/vtsint/faxrectest/faxin.php?baud=0&call%5Fstatus=3&csi=&facility=1&faxnumber=0866066918&filename=&filepath=&format=txproc&msg=&pages=0&remote%5Fstation=0879404100&seconds=37dropFaxhttp://php2/vtsint/faxrectest/faxin.php?baud=0&call%5Fstatus=3&csi=&facility=1&faxnumber=0866066918&filename=&filepath=&format=txproc&msg=&pages=0&remote%5Fstation=0879404100&seconds=37http://php2/vtsint/faxrectest/faxin.php?baud=0&call%5Fstatus=3&csi=&facility=1&faxnumber=0866066918&filename=&filepath=&format=txproc&msg=&pages=0&remote%5Fstation=0879404100&seconds=37http://php2/vtsint/faxrectest/faxin.php?baud=0&call%5Fstatus=3&csi=&facility=1&faxnumber=0866066918&filename=&filepath=&format=txproc&msg=&pages=0&remote%5Fstation=0879404100&seconds=37http://php2/vtsint/faxrectest/faxin.php?baud=0&call%5Fstatus=3&csi=&facility=1&faxnumber=0866066918&filename=&filepath=&format=txproc&msg=&pages=0&remote%5Fstation=0879404100&seconds=37http://php2/vtsint/faxrectest/faxin.php?baud=0&call%5Fstatus=3&csi=&facility=1&faxnumber=0866066918&filename=&filepath=&format=txproc&msg=&pages=0&remote%5Fstation=0879404100&seconds=37' );
$event->addParam( 'param1', '1http://php2/vtsint/faxrectest/faxin.php?baud=0&call%5Fstatus=3&csi=&facility=1&faxnumber=0866066918&filename=&filepath=&format=txproc&msg=&pages=0&remote%5Fstation=0879404100&seconds=37dropFaxhttp://php2/vtsint/faxrectest/faxin.php?baud=0&call%5Fstatus=3&csi=&facility=1&faxnumber=0866066918&filename=&filepath=&format=txproc&msg=&pages=0&remote%5Fstation=0879404100&seconds=37http://php2/vtsint/faxrectest/faxin.php?baud=0&call%5Fstatus=3&csi=&facility=1&faxnumber=0866066918&filename=&filepath=&format=txproc&msg=&pages=0&remote%5Fstation=0879404100&seconds=37http://php2/vtsint/faxrectest/faxin.php?baud=0&call%5Fstatus=3&csi=&facility=1&faxnumber=0866066918&filename=&filepath=&format=txproc&msg=&pages=0&remote%5Fstation=0879404100&seconds=37http://php2/vtsint/faxrectest/faxin.php?baud=0&call%5Fstatus=3&csi=&facility=1&faxnumber=0866066918&filename=&filepath=&format=txproc&msg=&pages=0&remote%5Fstation=0879404100&seconds=37http://php2/vtsint/faxrectest/faxin.php?baud=0&call%5Fstatus=3&csi=&facility=1&faxnumber=0866066918&filename=&filepath=&format=txproc&msg=&pages=0&remote%5Fstation=0879404100&seconds=37' );
$event->addParam( 'param2', '2http://php2/vtsint/faxrectest/faxin.php?baud=0&call%5Fstatus=3&csi=&facility=1&faxnumber=0866066918&filename=&filepath=&format=txproc&msg=&pages=0&remote%5Fstation=0879404100&seconds=37dropFaxhttp://php2/vtsint/faxrectest/faxin.php?baud=0&call%5Fstatus=3&csi=&facility=1&faxnumber=0866066918&filename=&filepath=&format=txproc&msg=&pages=0&remote%5Fstation=0879404100&seconds=37http://php2/vtsint/faxrectest/faxin.php?baud=0&call%5Fstatus=3&csi=&facility=1&faxnumber=0866066918&filename=&filepath=&format=txproc&msg=&pages=0&remote%5Fstation=0879404100&seconds=37http://php2/vtsint/faxrectest/faxin.php?baud=0&call%5Fstatus=3&csi=&facility=1&faxnumber=0866066918&filename=&filepath=&format=txproc&msg=&pages=0&remote%5Fstation=0879404100&seconds=37http://php2/vtsint/faxrectest/faxin.php?baud=0&call%5Fstatus=3&csi=&facility=1&faxnumber=0866066918&filename=&filepath=&format=txproc&msg=&pages=0&remote%5Fstation=0879404100&seconds=37http://php2/vtsint/faxrectest/faxin.php?baud=0&call%5Fstatus=3&csi=&facility=1&faxnumber=0866066918&filename=&filepath=&format=txproc&msg=&pages=0&remote%5Fstation=0879404100&seconds=37' );
$event->addParam( 'param3', '3http://php2/vtsint/faxrectest/faxin.php?baud=0&call%5Fstatus=3&csi=&facility=1&faxnumber=0866066918&filename=&filepath=&format=txproc&msg=&pages=0&remote%5Fstation=0879404100&seconds=37dropFaxhttp://php2/vtsint/faxrectest/faxin.php?baud=0&call%5Fstatus=3&csi=&facility=1&faxnumber=0866066918&filename=&filepath=&format=txproc&msg=&pages=0&remote%5Fstation=0879404100&seconds=37http://php2/vtsint/faxrectest/faxin.php?baud=0&call%5Fstatus=3&csi=&facility=1&faxnumber=0866066918&filename=&filepath=&format=txproc&msg=&pages=0&remote%5Fstation=0879404100&seconds=37http://php2/vtsint/faxrectest/faxin.php?baud=0&call%5Fstatus=3&csi=&facility=1&faxnumber=0866066918&filename=&filepath=&format=txproc&msg=&pages=0&remote%5Fstation=0879404100&seconds=37http://php2/vtsint/faxrectest/faxin.php?baud=0&call%5Fstatus=3&csi=&facility=1&faxnumber=0866066918&filename=&filepath=&format=txproc&msg=&pages=0&remote%5Fstation=0879404100&seconds=37http://php2/vtsint/faxrectest/faxin.php?baud=0&call%5Fstatus=3&csi=&facility=1&faxnumber=0866066918&filename=&filepath=&format=txproc&msg=&pages=0&remote%5Fstation=0879404100&seconds=37' );
my ($retVal,$errString) = $event->serialise( undef, $udSocket, undef, undef );
if( $retVal )
{
  print LOGFILE "serialse returned: $retVal\n";
} # if
else
{
  print LOGFILE "serialise failed: $errString\n";
} # else
