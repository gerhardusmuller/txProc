# general utilities
# 
# $Id: Utils.pm 3021 2014-03-10 18:19:24Z gerhardus $
# Versioning: a.b.c a is a major release, b represents changes or new features, c represents bug fixes. 
# @version 1.0.0		07/01/2010		Gerhardus Muller		Script created
# @version 1.1.0		06/07/2011		Gerhardus Muller		added the db parameters
# @version 1.2.0		15/02/2012		Gerhardus Muller		added the referenceToString
# @version 1.3.0		31/07/2012		Gerhardus Muller		updated referenceToString to use a json format
# @version 1.4.0		28/03/2013		Gerhardus Muller		added connectDb
# 
# Gerhardus Muller
package Utils;
use strict;
use vars qw(@ISA @EXPORT @EXPORT_OK %EXPORT_TAGS $VERSION);
#use lib '/home/vts/vts/perl/modules';
use Log qw( $timestamp *LOGFILE );
use B();

use Exporter;
$VERSION = 1.00;
@ISA = qw(Exporter);

our $dbRead;
our $dbMain;

@EXPORT_OK = qw( listToString propertiesToString hashToString referenceToString $dbRead $dbMain connectReadDb connectMasterDb connectDb query );

######
# $bLog - if not defined is taken to be true
sub query
{
  my ($dbh,$query,$fnName,$bLog) = @_;
  my $sth = $dbh->prepare( $query );
  my $execRetVal;
  if( !$sth )
  {
    print LOGFILE "$timestamp WARN $fnName: prepare error on query:$query - ".$dbh->errstr."\n";
    return;
  } # if
  $execRetVal = $sth->execute();  # undef for error; for non-select the number of rows affected
  if( !$execRetVal )
  {
    print LOGFILE "$timestamp WARN $fnName: execute error on query:$query - ".$dbh->errstr."\n";
    return;
  } # if
  print LOGFILE "$timestamp info  $fnName: queried:$query retVal:$execRetVal\n" if(!defined($bLog) || $bLog);
  return $sth;
} # query

######
# call with connectReadDb( getDatabase('vts1_read') );
sub connectReadDb
{
  my ($dbParams,$options) = @_;

  $dbRead = connectDb( $dbParams,$options );
  return $dbRead;
} # connectReadDb

######
# call with connectMasterDb( getDatabase('vts1_master') );
sub connectMasterDb
{
  my ($dbParams,$options) = @_;

  $dbMain = connectDb( $dbParams,$options );
  return $dbMain;
} # connectMasterDb

######
# call with connectDb( getDatabase('xxx') );
sub connectDb
{
  my ($dbParams,$options) = @_;
  if( !defined($dbParams) )
  {
    print LOGFILE "$timestamp ERROR connectDb: db params not found\n";
    return;
  } # if
  my $connectString = "DBI:mysql:database=$dbParams->{name};host=$dbParams->{server}";
  $connectString .= ";$options" if(defined($options));
  my $db = DBI->connect( $connectString,$dbParams->{user},$dbParams->{password} );
  if( $db )
  {
    $db->{mysql_auto_reconnect} = 1 ; # can't use this with transactions but should not be applicable here
    print LOGFILE "$timestamp info  connectDb: connected to database:$dbParams->{name} host:$dbParams->{server} dbUser:$dbParams->{user}\n";
    return $db;
  } # if
  else
  {
    print LOGFILE "$timestamp ERROR connectDb: failed to connect to database:$dbParams->{name} host:$dbParams->{server} dbUser:$dbParams->{user}\n";
    return undef;
  } # else
} # connectDb

# this is for the case where we are passed an arbitrary reference
# @param $ref
sub referenceToString
{
  my ($param) = @_;
  my $str = referenceToString1( $param );
  chop( $str ) if(substr($str,-1) eq ','); # strip off any comma after the closing
  return $str;
} # referenceToString
sub referenceToString1
{
  my ($param) = @_;
  my $str = "";
  if( ref($param) eq "HASH" )
  {
    $str .= '{';
    for my $key (sort keys (%$param))
    {
      $str .= "\"$key\":";
      my $value = $param->{$key};
      $str .= referenceToString1( $value );
    } # for
    chop( $str ) if(substr($str,-1) eq ','); # strip off any comma after the closing
    $str .= '}';
  } # if
  elsif( ref($param) eq "ARRAY" )
  {
    $str .= '[';
    for( my $i = 0; $i < scalar(@$param); $i++ )
    {
      my $value = @$param[$i];
      $str .= referenceToString1( $value );
    } # for
    chop( $str ) if(substr($str,-1) eq ','); # strip off any comma after the closing
    $str .= ']';
  } # elsif
  else
  {
    # differentiate between parameters referenced in string or numerical context the last time
    # http://search.cpan.org/~zefram/Params-Classify-0.013/lib/Params/Classify.pm
    # http://perldoc.perl.org/perlguts.html - What's Really Stored in an SV?
    # for an example of use see /root/.cpan/.cpan/build/JSON-2.53-U70dWb/lib/JSON/backportPP.pm function sub value_to_json
    if(defined($param))
    {
      my $b_obj = B::svref_2object(\$param);
      my $flags = $b_obj->FLAGS;    # SVp_IOK is an integer, SVp_NOK is a number, SVp_POK is a string
      if( $flags & B::SVp_POK )
      {
        $str .= "\"$param\"";
      } # if
      else
      {
        $param =~ s/"/\\"/g;
        $param =~ s/\\/\\\\/g;
        $str .= "$param";
      } # else
    } # if
    else
    {
      $str .= "null";
    } # else
  } # else
  $str .= ',';
  return $str;
} # referenceToString

# same as hashToString except it dumps the object itself - but still much the same ..
# @param $this - pass a reference to the object hash or any hash for that matter
# @return - string representation
sub propertiesToString
{
  my $this = shift;
  my $str = "";
  for my $key (sort keys (%$this))
  {
    my $value = $this->{$key};
    if( ref($value) eq "HASH" )
    {
      $str .= "HASH-$key=>".hashToString($value).",";
    } # if
    elsif( ref($value) eq "ARRAY" )
    {
      $str .= "ARRAY-$key=>".listToString($value).",";
    } # if
    else
    {
      if(defined($value))
      {
        $str .= "$key=>'$value',";
      } # if
      else
      {
        $str .= "$key=>undefined,";
      } # else
    } # else
  } # for
  chop( $str );  # it either ends on a ',' or the string is empty
  return $str;
} # propertiesToString

sub hashToString
{
  my ($param) = @_;
  my $text = "#[";
  if( ref($param) eq "HASH" )
  {
    for my $key (sort keys (%$param))
    {
      my $value = $param->{$key};
      if(defined($value))
      {
        if( ref($value) eq "HASH" )
        {
          $text .= "HASH-$key=>".hashToString($value).",";
        } # if
        elsif( ref($value) eq "ARRAY" )
        {
          $text .= "ARRAY-$key=>".listToString($value).",";
        } # if
        else
        {
          $text .= "$key=>'$value',";
        } # else
      } # if
      else
      {
        $text .= "$key=>undefined,";
      } # else
    } # for
  } # if
  else
  {
    warn "hashToString param $param is not a hash\n";
  } # else
  chop( $text ) if(length($text)>2);  # it either ends on a ',' or the string is empty
  $text .= "]#";
  return $text;
} # hashToString

sub listToString
{
  my ($param) = @_;
  my $text = "#[";
  if( ref($param) eq "ARRAY" )
  {
    for( my $i = 0; $i < scalar(@$param); $i++ )
    {
      my $value = @$param[$i];
      my $str;
      if(defined($value))
      {
        if( ref($value) eq "HASH" )
        {
          $str = hashToString($value);
        } # if
        elsif( ref($value) eq "ARRAY" )
        {
          $str = listToString($value);
        } # if
        else
        {
          $str .= $value;
        } # else
      } # if
      else
      {
        $str = 'undefined';
      } # else
      $text .= "i:$i=>'$str',";
    } # for
  } # if
  else
  {
    warn "listToString param $param is not a list\n";
  } # else
  chop( $text ) if(length($text)>2);  # it either ends on a ',' or the string is empty
  $text .= "]#";
  return $text;
} # listToString

1;  # module return value
