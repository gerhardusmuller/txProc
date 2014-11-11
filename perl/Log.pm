# Log support
#
# $Id: Log.pm 1344 2011-02-24 13:33:21Z gerhardus $
# Gerhardus Muller
#

package Log;
use strict;
use vars qw(@ISA @EXPORT @EXPORT_OK %EXPORT_TAGS $VERSION);
use Time::localtime;

use Exporter;
$VERSION = 1.00;
@ISA = qw(Exporter);

@EXPORT = qw( );
@EXPORT_OK = qw( generateTimestamp restoreStdErr redirectStdErr openLog closeLog $timeString $epochSecs $timestamp $logYear $logMonth $logDay *LOGFILE );
#%EXPORT_TAGS = (
#  TAG1 => [],
#  TAG2 => []
#);

#constants

#globals
our $timestamp = '';
our $logYear;
our $logMonth;
our $logDay;
our $beVerbose = 0;
our $olderr;
our $epochSecs = 0;
our $timeString = '';

#locals

sub generateTimestamp
{
  $epochSecs = time();
  my $tm = localtime( $epochSecs );
  $logYear = $tm->year()+1900;
  $logMonth = $tm->mon()+1;
  $logDay = $tm->mday();
  $timeString = sprintf("%02d%02d%04d %02d:%02d:%02d", $tm->mday, $logMonth, $logYear, $tm->hour, $tm->min, $tm->sec);
  $timestamp = sprintf("[%s %06d]", $timeString, $$);
  return $timestamp;
} # generateTimestamp

# open the log and generate a date stamp
# @param $logFile - if stderr then duplicate the STDERR handle
# @return LOGFILE, timestamp, $bSuccess
sub openLog
{
  my( $logFile ) = @_;
  my $bSuccess = 1;
  generateTimestamp();

  if( lc($logFile) eq 'stderr' )
  {
    if( !open( LOGFILE, ">&STDERR" ) )
    {
      print STDERR "openLog:cannot open outfile to STDERR: $!\n";
      $bSuccess = 0;
      return (*LOGFILE, $timestamp, $bSuccess);
    } # if
    select LOGFILE;
    $| = 1; # make unbuffered
  } # else
  else
  {
    if( !open( LOGFILE, ">>", $logFile ) )
    {
      print STDERR "openLog:cannot open outfile $logFile: $!\n";
      $bSuccess = 0;
      return (*LOGFILE, $timestamp, $bSuccess);
    } # if
  } # if

  return (*LOGFILE, $timestamp, $bSuccess);
} # openLog

# closes the logs again
sub closeLog( )
{
  close LOGFILE or die "Error closing LOGFILE: $! $?";
} # closeLog

# redirect STDERR to LOGFILE
sub redirectStdErr
{
  # redirect stderr to the log file to prevent mail module errors from creating bounce messages
  open( $olderr, '>&', \*STDERR );
  open( STDERR, '>&LOGFILE' ) or print LOGFILE "$timestamp ERROR redirectStdErr cannot redirect stderr: $!\n";
} # redirectStdErr

# restores STDERR again so that error output will go to stderr
sub restoreStdErr
{
  open( STDERR, '>&', $olderr );
} # restoreStdErr

1;  # module return value
__END__

=back

=head1 PURPOSE

This library contains fax handling support code

=head1 INTERNALS

To be written

=head1 AUTHOR

Faxplatform.za.net

Primary developer: Gerhardus Muller

=cut
