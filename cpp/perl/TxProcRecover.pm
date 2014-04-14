# txProc recovery lib
#
# $Id: TxProcRecover.pm 2911 2013-10-07 09:27:37Z gerhardus $
# Versioning: a.b.c a is a major release, b represents changes or new features, c represents bug fixes. 
# @version 1.0.0		03/10/2013		Gerhardus Muller		snapshot somewhere down the line

# Gerhardus Muller
#

package TxProcRecover;
use strict;
use vars qw(@ISA @EXPORT @EXPORT_OK %EXPORT_TAGS $VERSION);

use lib '/home/vts/vts/perl/modules';
use Log qw( $timestamp $logYear $logMonth $logDay *LOGFILE );
use TxProc;

use Exporter;
$VERSION = 1.00;
@ISA = qw(Exporter);

@EXPORT = qw( );
@EXPORT_OK = qw( scanLog setReportsVebose );

our $beVerbose = 0;

######
sub setReportsVebose
{
  $beVerbose = shift;
} # setReportsVebose

#####
# scans the recovery log
# @returns (1,$selectCount,undef) or (0,0,$err);
sub scanLog
{
  my ($bRecover,$bIgnore,$bSummarise,$bListDetail,$bCountRecoverableEvents,$bCombineWithOr,$recoveryLog,$recoveryTitle,$grepTerm,$classSelect,$queueSelect,$statusSelect,$errSelect,$limitNumEvents,$fuplTxProcUnixPath,$fuplTxProcName,$fuplTxProcService,$bDryrun,$bZeroRetries,$patchInstructions) = @_;

  if( $bRecover || $bIgnore )
  {
    return(0,0,"FAILED to open recovery log file '$recoveryLog'" ) if( !open( RECOVERYLOG, "+<", $recoveryLog ) );
    seek( RECOVERYLOG, 0, 2 );  # seek to the end of the file - doesn't seem to work to seek in a file opened for appending
    
    # make sure we can write to the file
    my $date = `date`;
    chomp( $date );
    $recoveryTitle .= " bDryrun=1" if($bDryrun);
    print RECOVERYLOG "\n$date: $recoveryTitle\n" or die( "FAILED to write to the recovery log file '$recoveryLog'" );
    seek( RECOVERYLOG, 0, 0 );
  } # if
  else
  {
    return(0,0,"FAILED to open recovery log file '$recoveryLog'" ) if( !open( RECOVERYLOG, "<", $recoveryLog ) );
  } # else

  my %objTypes;
  my %failReasons;
  my $recoverableEntries = 0;
  my $selectCount = 0;
  my $limitCount = 0;
  my $numRecovered = 0;
  my $numFailedRecovery = 0;
  my $recoveredLineNumbers = '';
  my $prevFtell = 0;
  my $validLines = 0;

  my $lineNo = 0;
  while( <RECOVERYLOG> )
  {
    my $line = $_;
    $lineNo++;
    #              SUCC     date    secs  error from  to    queue class path    logtime serialised event
    if( $line =~ /^(\w+)\s?,([^,]+),(\d+),(\w*),(\w+),(\w+),(\w*),(\w+),([^,]+),([^,]+),(.*)/ )
    {
      my $status = $1;
      my $date = $2;
      my $secs = $3;
      my $error = $4;
      my $from = $5;
      my $to = $6;
      my $queue = $7;
      my $class = $8;
      my $path = $9;
      my $logTimestamp = $10;
      my $serialisedEvent = $11;
      $validLines++;
      $error = 'unknown' if( length($error)==0 );
      print LOGFILE "$timestamp DEBUG scanLog: $lineNo: $status $date error:$error q:$queue class:$class path:$path ts:$logTimestamp\n" if( $beVerbose );

      if( $bSummarise )
      {
        $objTypes{$class} = 0 if( !exists( $objTypes{$class} ) );
        $objTypes{$class}++;
        $failReasons{$error} = 0 if( !exists( $failReasons{$error} ) );
        $failReasons{$error}++;
        $recoverableEntries++ if( $status eq 'SUCC' );
      } # if $bSummarise

      my $bSelected = 0;
      if( (length($grepTerm)==0)&&(length($classSelect)==0)&&(length($errSelect)==0)&&(length($queueSelect)==0)&&(length($statusSelect)==0))
      {
        $bSelected = 1;
      } # if
      else
      {
        if( $bCombineWithOr )
        {
          $bSelected = 1 if( (length($grepTerm) > 0)     && ($line =~ /$grepTerm/) );
          $bSelected = 1 if( (length($classSelect) > 0)  && ($class eq $classSelect) );
          $bSelected = 1 if( (length($errSelect) > 0)    && ($error eq $errSelect) );
          $bSelected = 1 if( (length($queueSelect) > 0)  && ($queue eq $queueSelect) );
          $bSelected = 1 if( (length($statusSelect) > 0) && ($status eq $statusSelect) );
        } # if OR
        else
        {
          $bSelected = 1 if( 
              ( (length($grepTerm)==0)    || ( (length($grepTerm) > 0)    && ($line =~ /$grepTerm/) ) ) &&
              ( (length($classSelect)==0) || ( (length($classSelect) > 0) && ($class eq $classSelect) ) ) &&
              ( (length($errSelect)==0)   || ( (length($errSelect) > 0)   && ($error eq $errSelect) ) ) &&
              ( (length($queueSelect)==0) || ( (length($queueSelect) > 0) && ($queue eq $queueSelect) ) ) &&
              ( (length($statusSelect)==0)|| ( (length($statusSelect) > 0)&& ($status eq $statusSelect) ) )
          );
        } # else AND
      } # else

      $selectCount++ if( $bSelected );
      if( $bSelected && ($status eq 'SUCC') )
      {
        $limitCount++;
        if( $beVerbose )
        {
          print LOGFILE "$timestamp INFO scanLog:selected $lineNo: $status $date err:$error q:$queue class:$class path:$path ts:$logTimestamp\n";
          print LOGFILE "$timestamp INFO scanLog:  $serialisedEvent\n" if( $bListDetail );
        } # if
      } # if listing

      if( $bSelected && ($bRecover||$bIgnore) && ($status eq 'SUCC') )
      {
        if( $bIgnore || recoverNow( $path,$fuplTxProcUnixPath,$fuplTxProcName,$fuplTxProcService,$bDryrun,$bZeroRetries,$patchInstructions ) )
        {
          my $logStr;
          $logStr = "recovered" if( $bRecover );
          $logStr = "marked ignored" if( $bIgnore );
          print LOGFILE "$timestamp INFO scanLog:$logStr $lineNo: $status $date err:$error q:$queue class:$class path:$path\n" if( $beVerbose );
          if( !$bDryrun )
          {
            my $currentPos = tell( RECOVERYLOG );
            seek( RECOVERYLOG, $prevFtell, 0 ) or warn "FAILED to seek to prevFtell:$prevFtell";  # seek to the beginning of the current line
            print( RECOVERYLOG 'REC ' ) if( $bRecover );
            print( RECOVERYLOG 'IGN ' ) if( $bIgnore );
            seek( RECOVERYLOG, $currentPos, 0 ) or warn "FAILED to seek to currentPos:$currentPos"; # seek to where we were
          } # if
          $numRecovered++;
          $recoveredLineNumbers .= "$lineNo,";
        } # if
        else
        {
          print LOGFILE "$timestamp WARN scanLog:FAILED recovery $lineNo: $status $date err:$error q:$queue class:$class path:$path\n";
          $numFailedRecovery++;
        } # else
      } # if recovering
    } # if
    $prevFtell = tell( RECOVERYLOG );
    last if(defined($limitNumEvents) && ($limitCount>=$limitNumEvents));
  } # while

  if( $bSummarise && !$bCountRecoverableEvents )
  {
    $/ = ",";
    chomp( $recoveredLineNumbers );
    seek( RECOVERYLOG, 0, 2 );  # seek to the end first in case we limited the number of recovery events we processed
    print RECOVERYLOG "Total of $recoverableEntries recoverable entries out of $validLines lines (total $lineNo), recovered $numRecovered failed recovery $numFailedRecovery, recovered line nos: $recoveredLineNumbers\n" if( $bRecover );
    print LOGFILE "$timestamp INFO scanLog:Total of $recoverableEntries recoverable entries out of $validLines lines (total $lineNo), recovered $numRecovered failed recovery $numFailedRecovery\n";
    print LOGFILE "$timestamp INFO scanLog:Selected $selectCount entries by search criteria\n";
    print LOGFILE "$timestamp INFO scanLog:Event type / class summary:\n";
    foreach my $key (keys(%objTypes))
    {
      print LOGFILE "$timestamp INFO scanLog:  $key: $objTypes{$key} lines\n";
    } # foreach
    print LOGFILE "$timestamp INFO scanLog:Failure reason summary:\n";
    foreach my $key (keys(%failReasons))
    {
      print LOGFILE "$timestamp INFO scanLog:  $key: $failReasons{$key} lines\n";
    } # foreach
  } # if $bSummarise

  close RECOVERYLOG;
  return(1,$selectCount,undef);
} # sub scanLog

######
# recover an event
sub recoverNow
{
  my ($eventFilename,$fuplTxProcUnixPath,$fuplTxProcName,$fuplTxProcService,$bDryrun,$bZeroRetries,$patchInstructions) = @_;
  my $bSuccess = 1;
  my $objToSubmit = '';

  if( open( OBJ, "< $eventFilename" ) )
  {
    my $oldSlurp = $/;
    undef $/; # enter slurp mode
    $objToSubmit = <OBJ>;
    $/ = $oldSlurp;
    close OBJ;
  } # if
  else
  {
    my $fatalReason = "unable to open object input file '$eventFilename'";
    print LOGFILE "$timestamp ERROR recoverNow:$fatalReason\n";
    return 0;
  }  # else

  # we are now supposed to have a serialised object
  # verify validity of the object
  my ($recoveryEvent,$parseError) = TxProc->unSerialiseFromString( $objToSubmit );
  if( !$recoveryEvent )
  {
    my $fatalReason = "illegal object $parseError";
    print LOGFILE "$timestamp ERROR recoverNow:$fatalReason\n";
    return 0;
  } # if

  # zero the retry counter if requested
  $recoveryEvent->retries(0) if($bZeroRetries);

  # patch the event if requested
  patchEvent( $recoveryEvent, $patchInstructions ) if( defined($patchInstructions) );

  #$recoveryEvent->beVerbose(1);
  if( !$bDryrun )
  {
    my ($retVal,$submitErrorString) = $recoveryEvent->serialise( undef,$fuplTxProcUnixPath,$fuplTxProcName,$fuplTxProcService );
    if( !$retVal )
    {
      print LOGFILE "$timestamp ERROR recoverNow:failed to submit to txProc: '$submitErrorString'\n";
      return 0;
    } # if
    else
    {
      unlink $eventFilename;
    } # else
  } # if
  else
  {
    print LOGFILE "$timestamp INFO recoverNow:bDryrun set - event:".$recoveryEvent->toString()."\n";
  } # else

  return $bSuccess;
} # recoverNow

######
# $patchInstructions is an array of patch instructions each of the format
#   { field => 'name', cmd => 'regex|regexparam|regexscript|dropparam|addparam|prependscript|deletescript|cat|catparam|catscript', value => 'parameter to cmd' }
# the *param commands all operate on named event parameters
# the *script commands all operate on event script parameters
# the cat* commands displays the value of the parameter on stdout (cat cats parameters such as url,scriptName,destQueue)
# regexparam operates on a param. in this case the field is the parameter name
# regexscript operates on scriptparams. in this case the field is the parameter index with 0 the first parameter
# for regex field has to resolve to a method name in the TxProc class for regex (examples are url,scriptName,destQueue)
# any regex based command should provide the regex as the value - an example would be '/replacethis/forthat/' including the forward slashes and modifiers
sub patchEvent
{
  my ($event,$patchInstructions) = @_;
  my $ref = $event->reference();
  foreach my $instruction (@$patchInstructions)
  {
    my $cmd = $instruction->{cmd};
    my $fieldname = $instruction->{field};
    my $value = $instruction->{value};

    if( $cmd eq 'regex' )
    {
      my $fieldvalue = $event->$fieldname();
      if( defined($fieldvalue) )
      {
        my $newFieldvalue = $fieldvalue;
        eval( '$newFieldvalue =~ s'.$value );
        $event->$fieldname( $newFieldvalue );
        print LOGFILE "$timestamp INFO patchEvent: ref:$ref fieldname:$fieldname cmd:$cmd cmdValue:$value fieldValue:$fieldvalue new:$newFieldvalue\n";
      } # if
      else
      {
        print LOGFILE "$timestamp WARN patchEvent: ref:$ref cmd:$cmd fieldname:$fieldname does not exist\n";
      } # else
    } # if
    elsif( $cmd eq 'regexparam' )
    {
      my $fieldvalue = $event->getParam( $fieldname );
      if( defined($fieldvalue) )
      {
        my $newFieldvalue = $fieldvalue;
        eval( '$newFieldvalue =~ s'.$value );
        $event->addParam( $fieldname, $newFieldvalue );
        print LOGFILE "$timestamp INFO patchEvent: ref:$ref paramname:$fieldname cmd:$cmd cmdValue:$value fieldValue:$fieldvalue new:$newFieldvalue\n";
      } # if
      else
      {
        print LOGFILE "$timestamp WARN patchEvent: ref:$ref cmd:$cmd paramname:$fieldname does not exist\n";
      } # else
    } # elsif
    elsif( $cmd eq 'regexscript' )
    {
      # $fieldname has to be a script param index
      my $fieldvalue = $event->getScriptParam( $fieldname );
      if( defined($fieldvalue) )
      {
        my $newFieldvalue = $fieldvalue;
        eval( '$newFieldvalue =~ s'.$value );
        $event->setScriptParam( $fieldname, $newFieldvalue );
        print LOGFILE "$timestamp INFO patchEvent: ref:$ref paramindex:$fieldname cmd:$cmd cmdValue:$value fieldValue:$fieldvalue new:$newFieldvalue\n";
      } # if
      else
      {
        print LOGFILE "$timestamp WARN patchEvent: ref:$ref cmd:$cmd paramindex:$fieldname does not exist\n";
      } # else
    } # elsif
    elsif( $cmd eq 'dropparam' )
    {
      $event->deleteParam( $fieldname );
      print LOGFILE "$timestamp INFO patchEvent: ref:$ref cmd:$cmd fieldname:$fieldname\n";
    } # elsif
    elsif( $cmd eq 'addparam' )
    {
      $event->addParam( $fieldname, $value );
      print LOGFILE "$timestamp INFO patchEvent: ref:$ref cmd:$cmd fieldname:$fieldname value:$value\n";
    } # elsif
    elsif( $cmd eq 'prependscript' )
    {
      $event->prependScriptParam( $value );
      print LOGFILE "$timestamp INFO patchEvent: ref:$ref cmd:$cmd value:$value\n";
    } # elsif
    elsif( $cmd eq 'deletescript' )
    {
      $event->deleteScriptParam( $fieldname );
      print LOGFILE "$timestamp INFO patchEvent: ref:$ref cmd:$cmd fieldindex:$fieldname\n";
    } # elsif
    elsif( $cmd eq 'cat' )
    {
      my $fieldvalue = $event->$fieldname();
      if( defined($fieldvalue) )
      {
        print STDOUT $fieldvalue;
        print LOGFILE "$timestamp INFO patchEvent: ref:$ref paramname:$fieldname cmd:$cmd cmdValue:$value fieldValue:$fieldvalue\n";
      } # if
      else
      {
        print LOGFILE "$timestamp WARN patchEvent: ref:$ref cmd:$cmd paramname:$fieldname does not exist\n";
      } # else
    } # elsif
    elsif( $cmd eq 'catparam' )
    {
      my $fieldvalue = $event->getParam( $fieldname );
      if( defined($fieldvalue) )
      {
        print STDOUT $fieldvalue;
        print LOGFILE "$timestamp INFO patchEvent: ref:$ref paramname:$fieldname cmd:$cmd fieldValue:$fieldvalue\n";
      } # if
      else
      {
        print LOGFILE "$timestamp WARN patchEvent: ref:$ref cmd:$cmd paramname:$fieldname does not exist\n";
      } # else
    } # elsif
    elsif( $cmd eq 'catscript' )
    {
      # $fieldname has to be a script param index
      my $fieldvalue = $event->getScriptParam( $fieldname );
      if( defined($fieldvalue) )
      {
        print STDOUT $fieldvalue;
        print LOGFILE "$timestamp INFO patchEvent: ref:$ref paramindex:$fieldname cmd:$cmd fieldValue:$fieldvalue\n";
      } # if
      else
      {
        print LOGFILE "$timestamp WARN patchEvent: ref:$ref cmd:$cmd paramindex:$fieldname does not exist\n";
      } # else
    } # elsif
    else
    {
      print LOGFILE "WARN patchEvent cmd:$cmd not recognising - ignoring\n";
    } # else
  } # foreach
} # patchEvent

1;  # module return value
__END__

=back

=head1 PURPOSE

txProc recovery lib

=head1 INTERNALS

To be written

=head1 AUTHOR

Faxplatform.za.net

Primary developer: Gerhardus Muller

=cut
