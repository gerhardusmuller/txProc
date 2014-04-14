#!/usr/bin/perl -w
# $Id: test.pl 1 2009-08-17 12:36:47Z gerhardus $
#
# Test scripts for telcoServe / generic
# perl -MCPAN -e "install Date::Calc"
# Gerhardus Muller
#

use strict;
use Getopt::Std;
use Time::gmtime;
use Date::Calc qw(Delta_DHMS);
use lib '/home/hylafax/platformV3/perlModules';
use ServerCommand;

# parse the command line options
our $basename = "txDelta";
our $beVerbose = 0;
my %option = ();
getopts( "hvst:b:", \%option );
# -v is verbose
$beVerbose = 1 if( exists( $option{v} ) );
# -h asks for help
if( exists( $option{h} ) )
{
  print( "$0 options\n" );
  print( "\t-s stop server\n" );
  print( "\t-t testno\n" );
  print( "\t-b basename - default $basename\n" );
  print( "\t-v for verbose output\n" );
  print( "\t-h for this help screen\n" );
  print( "\nRefer to the document testing.txt for a description of the tests\n" );
  exit( 1 );
} # help

my $testNo = 0;
$testNo = $option{t} if( exists( $option{t} ) );
$basename = $option{b} if( exists( $option{b} ) );
our $execFile = "/usr/local/bin/$basename";
our $initScript = "/etc/init.d/$basename";
our $pidFile = "/var/run/$basename.pid";
our $logDir = "/var/log/$basename";
our $socketpath = "$logDir/$basename.sock";
our $mainLog = "$logDir/$basename.log";
our $controllerLog = "$logDir/controller.log";
our $dispatcherLog = "$logDir/dispatcher.log";
our $socketLog = "$logDir/socket.log";
our $recoveryLog = "$logDir/recovery.log";
our $genExecFile = "./genEvents.pl";
our $genExecPidFile = "/var/run/genEvents.pid";
our $genExecLogFile = "/var/log/genEvents.log";
our $valgrindExec = "valgrind";
our $startTime = 0;
our $endTime = 0;
our $elapsedTime = 0;
our @connectDetails = (undef,undef,undef,$socketpath,1);

# stop the server and exit if requested
if( exists( $option{s} ) )
{
  stopServer( 1 );
  exit 0;
} # if

# execute the requested test
die( "no test selected" ) if( $testNo == 0 );
exit( eval( "testSub$testNo" ) );

######
# basic startup / shutdown / kill procedures
sub testSub1
{
  print "testSub1: basic startup / shutdown / kill procedures\n";
  my $result = 0;
  my $output = startServer( "test1.cfg" );
  print "testSub1: $output\nreturn code $?\n";
  $result += $?;
  sleep( 2 );
  my $cmd = "ps ax | grep '$execFile' | grep -v grep";
  $output = `$cmd`;
  my ($numLines,$splitOutput) = countLines( $output );
  my $expectedLines = 12;
  $result++ if( ($expectedLines-$numLines) != 0 );
  print "testSub1: should see $expectedLines processes - counted $numLines instances\ncmd:$cmd\n${output}overall result: $result\n";

  $result += stopServer( 0 );

  $result += checkMainLog( $mainLog );
  $result += checkDispatcherLog( $dispatcherLog );
  $result += checkSocketLog( $socketLog );
  print "testSub1: overall result: $result\n";
  return $result;
} # testSub1

######
# restarting of children that have died
sub testSub2
{
  print "testSub2: restarting of children that have died\n";
  my $result = 0;
  my $output = startServer( "test1.cfg" );
  sleep( 2 );
  my $pid = getLogParam( $mainLog, "forkDispatcher", "dispatcher has pid (\\d+)" );
  if( !defined($pid) || ($pid <= 1) )
  {
    $result++;
    print "testSub2: failed to retrieve dispatcher pid - overall result: $result\n";
    return $result;
  } # if

  # kill it repetatively
  for( my $count = 0; $count < 10; $count++ )
  {
    print( "testSub2: trying to kill dispatcher process pid '$pid'\n" );
    my $cmd = "kill $pid";
    print "testSub2 $count: $cmd - '".`$cmd`."'\n";
    sleep( 1 );
    my $donePid = getLogParam( $mainLog, "handleChildDone:", "child (\\d+) exited" );
    if( !defined($donePid) || ($donePid <= 1) )
    {
      $result++;
      print "testSub2: failed to kill dispatcher pid $pid - overall result: $result\n";
    } # if
    elsif( $pid != $donePid )
    {
      $result++;
      print "testSub2: tried to kill dispatcher pid $pid, actually killed $donePid - overall result: $result\n";
    } # if
    else
    {
      print "testSub2: process $donePid was killed and detected\n";
    } # else
    my $newPid = getLogParam( $mainLog, "forkDispatcher", "dispatcher has pid (\\d+)" );
    if( $newPid == $pid )
    {
      $result++;
      print "testSub2: failed to respawn killed dispatcher pid $pid - overall result: $result\n";
    } # if
    $pid = $newPid;
    print "testSub2 respawned process id is $pid\n";
    print "testSub2 $count:done\n\n";
    sleep( 1 );
  } # for

  $result += stopServer( 0 );

  print "testSub2: overall result: $result\n";
  return $result;
} # testSub2

######
# verify that the stats timer is running
sub testSub3
{
  print "testSub3: verify that the stats timer is running\n";
  my $result = 0;
  my $sleepTime = 30;
  my $output = startServer( "test3.cfg" );
  print "testSub3: sleeping for $sleepTime seconds\n";
  sleep( $sleepTime );
  
  my $cmd = "cat $mainLog | grep 'received a CMD_TIMER_SIGNAL' | wc -l";
  $output = `$cmd`;
  chomp( $output );
  my $numTimers = $output;
  if( $numTimers > 0 )
  {
    print( "testSub3: '$cmd' - counted $output lines\n" );
  } #
  else
  {
    $result++;
    print( "testSub3: failed '$cmd' - counted NO lines\n" );
  } # else
  
  $result += stopServer( 0 );

  print "testSub3: overall result: $result\n";
  return $result;
} # testSub3

######
# checks the logs for any errors
# checks for defunct processes
sub testSub4
{
  print "testSub4: check logs for errors and defunct processes\n";
  my $result = 0;
  $result += checkMainLog( $mainLog );
  $result += checkDispatcherLog( $dispatcherLog );
  $result += checkSocketLog( $socketLog );

  # check for defunct processes
  $execFile =~ /([^\/]+)$/;
  my $base = $1;
  my $cmd = "ps ax | grep '\\[$base\\] <defunct>' | grep -v grep | wc -l";
  print "testSub4: executing '$cmd'\n" if( $beVerbose );
  my $output = `$cmd`;
  chomp( $output );
  if( $output != 0 )
  {
    $result++;
    print "testSub4: WARN defunct process found\n";
  } # if
  
  print "testSub4: overall result: $result\n";
  return $result;
} # testSub4

######
# startup of queues with workers
sub testSub100
{
  print "testSub100: startup of queues with workers\n";
  my $result = 0;
  my $cfgFile = "test100.cfg";
  my $output = startServer( $cfgFile );
  sleep( 2 );

  my ($res,$queues) = parseDispatcherQueue( $dispatcherLog, $cfgFile );
  $result += $res;

  $result += stopServer( 0 );
  print "testSub100: overall result: $result\n";
  return $result;
} # testSub100

######
# startup of queues with workers
sub testSub101
{
  print "testSub101: loading of queues with different work loads\n";
  my $result = 0;
  my $output;
  my $cfgFile = "test100.cfg";
  my $genEventCfg = "genEvents101.cfg";
  my $bRunTest = 1;   # used to debug log file parsing

  if( $bRunTest )
  {
    $output = startServer( $cfgFile );
    sleep( 1 );

    # generate test events
    $startTime = time();
    print "startTime: $startTime\n";
    my $genOutput = startGenEvents( $genEventCfg );
    my $genPid = 0;
    $genPid = $1 if( $genOutput =~ /has pid (\d+)/ );
    if( $genPid == 0 )
    {
      $result++;
      print "testSub101: failed to start the event generator - no pid\n";
      return $result;
    } # if

    # wait for the test event generator to exit
    waitGenEventsExit();
    # wait for the server to stop and exit
    $result += stopServer( 1 );
    printElapsedTime();
  } # if

  $result += compareDispatcherProcessedEvents( $dispatcherLog, $genEventCfg, $recoveryLog );
  print "testSub101: overall result: $result\n";
  return $result;
} # testSub101

######
# overloading a queue should cause a recovery dump
# support for dynamic max queue length adjustment
# eventQueue dumpList
sub testSub102
{
  print "testSub102: overloading a queue; dynamic max queue length adjustment\n";
  my $result = 0;
  my $output;
  my $cfgFile = "test101.cfg";
  my $genEventCfg = "genEvents102.cfg";
  my $bRunTest = 1;   # used to debug log file parsing

  if( $bRunTest )
  {
    $output = startServer( $cfgFile );
    sleep( 1 );

    # generate test events
    $startTime = time();
    print "startTime: $startTime\n";
    my $genOutput = startGenEvents( $genEventCfg );
    my $genPid = 0;
    $genPid = $1 if( $genOutput =~ /has pid (\d+)/ );
    if( $genPid == 0 )
    {
      $result++;
      print "testSub102: failed to start the event generator - no pid\n";
      return $result;
    } # if
    else
    {
      print "testSub102: started event generator with pid $genPid\n";
    } # else

    # wait a while and then up the recovery limit on the msnreq queue
    my ($res,$tailResults) = tailLogFor( $dispatcherLog, "dumpList: processed \\d+ entries,.*queue '(\\w+)'", 20 );
    $result += $res;
    if( defined( $tailResults ) && (scalar(@$tailResults)>0) )
    {
      my $queue = @$tailResults[0];
      print "updating max queue length for queue '$queue'\n";
      my $txDeltaCmd = new ServerCommand( @connectDetails );
      my ($retVal,$submitErrorString) = $txDeltaCmd->setMaxQueueLen( $queue, 200 );
    } # if
    else
    {
      if( defined( $tailResults ) )
      {
        print "tailLogFor return type ".ref( $tailResults ). " length:".scalar(@$tailResults)." value[0]:".@$tailResults[0]."\n";
      } # if
      else
      {
        print "tailLogFor returned undef\n";
      } # else
    } # else
    
    # wait for the test event generator to exit
    waitGenEventsExit();
    # wait for the server to stop and exit
    $result += stopServer( 1 );
    printElapsedTime();
  } # if

  $result += compareDispatcherProcessedEvents( $dispatcherLog, $genEventCfg, $recoveryLog );

  print "testSub102: overall result: $result\n";
  return $result;
} # testSub102

######
# dynamic update of worker pool sizes
sub testSub103
{
  print "testSub103: dynamic update of worker pool sizes - ignore overall result\n";
  my $result = 0;
  my $output;
  my $cfgFile = "test101.cfg";
  my $genEventCfg = "genEvents103.cfg";
  my $bRunSubtest1 = 1;
  my $bRunSubtest2 = 1;
  my $bRunSubtest3 = 1;
  my $bRunSubtest4 = 1;
  my $num0Processes;
  my $genOutput;
  my $newTotal;
  my $cmdParams;
  my $runningProcCmd = "ps ax | grep '$execFile' | grep -v grep";
  my $numInitialProcesses;
  my $splitOutput;
  my ($queues,$totWorkers) = parseDispatcherQueueConf( $cfgFile );
  # select a queue to use
  # my ($res,$queues) = parseDispatcherQueue( $dispatcherLog, $cfgFile );
  my @queueNames = keys %$queues;
  my $queue = shift @queueNames;
  $queue = shift @queueNames if( $queue eq 'default' );
  if( $queue eq 'default' )
  {
    warn "testSub103: configure at least one queue other than 'default'\n";
    return 1;
  } # if
  my $queueEntry = $queues->{$queue};
  my $numStandardWorkers = $queueEntry->{numWorkers};

  #
  # subtest 1
  # reduce the number of workers to 0 and load with events to process
  if( $bRunSubtest1 )
  {
    $output = startServer( $cfgFile );
    sleep( 1 );
    $startTime = time();

    $output = `$runningProcCmd`;
    ($numInitialProcesses,$splitOutput) = countLines( $output );
    print "\n\n\ntestSub103: started $numInitialProcesses processes\n";
    print "###### - subtest 1: the selected queue workers are reduced to 0 (under no load) and then given events to execute - these should all land in the recovery log. despite the error for 'found no entries' on the selected queue the processed numbers should match\n";


    print "selected queue: '$queue' with $numStandardWorkers workers to use\n";
    print "reducing the workers to 0\n";
    my $txDeltaCmd = new ServerCommand( @connectDetails );
    my ($retVal,$submitErrorString) = $txDeltaCmd->setNumWorkers( $queue, 0 );
    sleep 1;
    $output = `$runningProcCmd`;
    ($num0Processes,$splitOutput) = countLines( $output );
    $newTotal = $numInitialProcesses - $numStandardWorkers;
    print "testSub103: now have $num0Processes processes should be $newTotal\n";

    # generate a couple of events that should sit in the queue
    $genEventCfg = "genEvents103.cfg";
    $genOutput = startGenEvents( $genEventCfg );
    waitGenEventsExit();
    sleep 1;

    # wait for the server to stop and exit
    $result += stopServer( 1 );
    $result += compareDispatcherProcessedEvents( $dispatcherLog, $genEventCfg, $recoveryLog );
    printElapsedTime();
  } # if subtest 1

  # subtest 2
  # reduce the number of workers to 0 (under no load), then load with events to process
  # add workers and make sure they are processed
  if( $bRunSubtest2 )
  {
    $output = startServer( $cfgFile );
    sleep 1;
    $startTime = time();

    $output = `$runningProcCmd`;
    ($numInitialProcesses,$splitOutput) = countLines( $output );
    print "\n\n\ntestSub103: started $numInitialProcesses processes\n";
    print "###### - subtest 2: reduce the number of workers to 0 (under no load), then load with events to process - add workers and make sure they are processed\n";

    print "selected queue: '$queue' with $numStandardWorkers workers to use\n";
    print "reducing the workers to 0\n";
    my $txDeltaCmd = new ServerCommand( @connectDetails );
    my ($retVal,$submitErrorString) = $txDeltaCmd->setNumWorkers( $queue, 0 );
    sleep 1;
    $output = `$runningProcCmd`;
    ($num0Processes,$splitOutput) = countLines( $output );
    $newTotal = $numInitialProcesses - $numStandardWorkers;
    print "testSub103: now have $num0Processes processes should be $newTotal\n";

    # generate a couple of events that should sit in the queue
    $genEventCfg = "genEvents103.cfg";
    $genOutput = startGenEvents( $genEventCfg );
    waitGenEventsExit();
    sleep 1;

    # increase the workers again
    $txDeltaCmd = new ServerCommand( @connectDetails );
    ($retVal,$submitErrorString) = $txDeltaCmd->setNumWorkers( $queue, 5 );
    sleep 1;
    $output = `$runningProcCmd`;
    ($num0Processes,$splitOutput) = countLines( $output );
    $newTotal = $numInitialProcesses - $numStandardWorkers + 5;
    print "testSub103: now have $num0Processes processes should be $newTotal\n";

    # wait for the server to stop and exit
    $result += stopServer( 1 );
    $result += compareDispatcherProcessedEvents( $dispatcherLog, $genEventCfg, $recoveryLog );
    printElapsedTime();
  } # subtest 2

  # subtest 3
  # decrease the number of workers under load - use the msnreq queue - otherwise
  # fallback to whatever was used before
  if( $bRunSubtest3 )
  {
    $output = startServer( $cfgFile );
    sleep 1;
    $startTime = time();
    @queueNames = keys %$queues;
    if( exists( $queues->{'msnreq'} ) )
    {
      $queue = 'msnreq';
      $queueEntry = $queues->{$queue};
      $numStandardWorkers = $queueEntry->{numWorkers};
    } # if
    print "\n\n\n###### -  subtest 3 - altering worker pool under load - using queue: '$queue' with $numStandardWorkers workers to use\n";
    $output = `$runningProcCmd`;
    ($numInitialProcesses,$splitOutput) = countLines( $output );
    print "starting with $numInitialProcesses processes\n";
    print "tail -f dispatcher.log | grep 'queue stats' to follow queue lengths\n";

    # enable a long queue
    print "updating max queue length for queue '$queue'\n";
    my $txDeltaCmd = new ServerCommand( @connectDetails );
    my ($retVal,$submitErrorString) = $txDeltaCmd->setMaxQueueLen( $queue, 200000 );

    # start the event generator - it runs continuously for the msnreq queue
    $genEventCfg = "genEvents103a.cfg";
    $genOutput = startGenEvents( $genEventCfg );
    sleep( 1 );

    for( my $i = 0; $i < 10; $i++ )
    {
      my $newNumProcesses = int(rand(101));
      my $txDeltaCmd = new ServerCommand( @connectDetails );
      my ($retVal,$submitErrorString) = $txDeltaCmd->setNumWorkers( $queue, $newNumProcesses );
      print "adjusting to $newNumProcesses for queue '$queue'\n";
      sleep 20;
      $output = `$runningProcCmd`;
      ($num0Processes,$splitOutput) = countLines( $output );
      $newTotal = $numInitialProcesses - $numStandardWorkers + $newNumProcesses;
      print "testSub103: now have $num0Processes processes should be $newTotal\n";
    } # for

    stopGenEvents();
    waitGenEventsExit();
    sleep 1;

    # wait for the server to stop and exit
    $result += stopServer( 1 );
    $result += compareDispatcherProcessedEvents( $dispatcherLog, $genEventCfg, $recoveryLog );
    printElapsedTime();
  } # if subtest3

  if( $bRunSubtest4 )
  {
    $output = startServer( $cfgFile );
    sleep 1;
    $startTime = time();
    @queueNames = keys %$queues;
    if( exists( $queues->{'msnreq'} ) )
    {
      $queue = 'msnreq';
      $queueEntry = $queues->{$queue};
      $numStandardWorkers = $queueEntry->{numWorkers};
    } # if
    print "\n\n\n###### - subtest 4 - test with 2 workers, then 50 and 2 again - confirm apache is following suite - using queue: '$queue' with $numStandardWorkers workers\n";
    $output = `$runningProcCmd`;
    ($numInitialProcesses,$splitOutput) = countLines( $output );
    print "starting with $numInitialProcesses processes\n";
    print "tail -f dispatcher.log | grep 'queue stats' to follow queue lengths\n";
    print "ps ax | grep apache | wc -l to confirm number of apache processes\n";

    # enable a long queue
    print "updating max queue length for queue '$queue'\n";
    my $txDeltaCmd = new ServerCommand( @connectDetails );
    my ($retVal,$submitErrorString) = $txDeltaCmd->setMaxQueueLen( $queue, 200000 );

    # start the event generator - it runs continuously for the msnreq queue
    $genEventCfg = "genEvents103b.cfg";
    $genOutput = startGenEvents( $genEventCfg );
    sleep( 1 );

    my @numProcesses = (2,50,2);
    foreach my $newNumProcesses (@numProcesses)
    {
      my $txDeltaCmd = new ServerCommand( @connectDetails );
      my ($retVal,$submitErrorString) = $txDeltaCmd->setNumWorkers( $queue, $newNumProcesses );
      print "adjusting to $newNumProcesses for queue '$queue'\n";
      sleep 5;
      $output = `$runningProcCmd`;
      ($num0Processes,$splitOutput) = countLines( $output );
      $newTotal = $numInitialProcesses - $numStandardWorkers + $newNumProcesses;
      print "testSub103: now have $num0Processes processes should be $newTotal\n";
      sleep 30;
    } # foreach

    stopGenEvents();
    waitGenEventsExit();
    sleep 1;

    # wait for the server to stop and exit
    $result += stopServer( 1 );
    $result += compareDispatcherProcessedEvents( $dispatcherLog, $genEventCfg, $recoveryLog );
    printElapsedTime();
  } # if subtest4
  print "testSub103: overall result: $result\n";
  return $result;
} # testSub103


######
# verification of the delay queue
sub testSub104
{
  print "testSub104: verification of the delay queue\nThis is not an automated test - manual inspection of the log files are required\n";
  my $result = 0;
  my $cfgFile = "test104.cfg";
  my $queue = "perl";
  my $output = startServer( $cfgFile );
  sleep 1;

  ## subtest 1
  print "###### - subtest 1: verification of delay on queue '$queue'\n";

  # generate a couple of events that should sit in the queue
  my $genEventCfg = "genEvents104.cfg";
  my $genOutput = startGenEvents( $genEventCfg );
  waitGenEventsExit();
  sleep 1;

  # grep for some events in the log
  print "should see the 5 events inserted into the delay queue:\n";
  print `cat $dispatcherLog | grep 'inserted delayed event'`;
  sleep 5*2+2;  # 5 is the number of test events, each delayed by its cycleNo*2, +2 is for good luck
  $result += stopServer(1);
  print "\nevent execution records - please note the execution times and lag (lateness):\n";
  print `cat $dispatcherLog | grep 'feedWorker: queue'`;

  print "testSub104: overall result: $result\n";
  return $result;
} # testSub104


######
# verification of the delay queue recovery events
sub testSub104a
{
  print "testSub104a: verification of the delay queue recovery\nThis is not an automated test - manual inspection of the log files are required\n";
  my $result = 0;
  my $cfgFile = "test104.cfg";
  my $queue = "perl";
  my $output = startServer( $cfgFile );
  sleep 1;

  ## subtest 1
  print "###### - subtest 1: verification of delay on queue '$queue'\n";

  # generate a couple of events that should sit in the queue
  my $genEventCfg = "genEvents104.cfg";
  my $genOutput = startGenEvents( $genEventCfg );
  waitGenEventsExit();
  sleep 1;

  # grep for some events in the log
  print "should see the 5 events inserted into the delay queue:\n";
  print `cat $dispatcherLog | grep 'inserted delayed event'`;
  $result += stopServer(0);
  print "\nmost of the events should land in the recovery log\n";
  print `cat $dispatcherLog | grep 'recoveryLog writeEntry'`;
  print "\n\nrecover using $execFile -N -r /var/log/txDelta/recovery.log\n";
  print "or use txDeltaRecover.pl\n";
  print "by waiting a while all events should execute immediately on recovery\n";
  #
  print "testSub104a: overall result: $result\n";
  return $result;
} # testSub104a


######
# verification of recovery events
sub testSub106
{
  print "testSub106: verification of recovery events\nrun either the recovery script or ./txDelta -N -r /var/log/txDelta/recovery.log\n";
  my $result = 0;
  my $cfgFile = "test104.cfg";
  my $output = startServer( $cfgFile );
  sleep 1;

  # generate a couple of events of which a percentage should fail
  my $genEventCfg = "genEvents106.cfg";
  my $genOutput = startGenEvents( $genEventCfg );
  waitGenEventsExit();
  sleep 1;

  $result += stopServer(0);

  print "testSub106: overall result: $result\n";
  return $result;
} # testSub106

######
# verification of the delay queue
sub testSub108
{
  print "testSub108: verification of event expiry times\nThis is not an automated test - manual inspection of the log files are required\n";
  my $result = 0;
  my $cfgFile = "test104.cfg";    # we are mainly after the 1s interval maintenance timer
  my $queue = "perl";
  my $output = startServer( $cfgFile );
  sleep 1;

  ## subtest 1
  print "###### - subtest 1: verification of delay on queue '$queue'\n";

  # generate a couple of events that should sit in the queue - most should exipire
  my $genEventCfg = "genEvents108.cfg";
  my $genOutput = startGenEvents( $genEventCfg );
  waitGenEventsExit();
  sleep 1;

  # grep for some events in the log
  $result += stopServer(1);
  print "should see the expired events:\n";
  print `cat $dispatcherLog | grep 'scanForExpiredEvents: queue'`;

  print "testSub108: overall result: $result\n";
  return $result;
} # testSub108


######
# verification of max execution times and the adjustment thereof
sub testSub109
{
  print "testSub109: verification of max execution times and the adjustment thereof\nThis is not an automated test - manual inspection of the log files are required\n";
  my $result = 0;
  my $cfgFile = "test109.cfg";
  my $queue = "perl";
  my $maxTime = 30;
  my $output = startServer( $cfgFile );
  sleep 1;

  ## subtest 1
  print "###### - subtest 1: execution time ${maxTime}s for queue '$queue'\n";
  my $txDeltaCmd = new ServerCommand( @connectDetails );
  my ($retVal,$submitErrorString) = $txDeltaCmd->setMaxExecTime( $queue, $maxTime );

  # generate a couple of events that should sit in the queue - all should expire
  my $genEventCfg = "genEvents109.cfg";
  my $genOutput = startGenEvents( $genEventCfg );
  waitGenEventsExit();
  sleep $maxTime+15;    # based on a maintenance period of 10s

  ## subtest 2
  $queue = "perl";
  $maxTime = 15;
  print "\n\n###### - subtest 2: execution time ${maxTime}s for queue '$queue'\n";
  $txDeltaCmd = new ServerCommand( @connectDetails );
  ($retVal,$submitErrorString) = $txDeltaCmd->setMaxExecTime( $queue, $maxTime );

  # generate a couple of events that should sit in the queue - all should expire
  $genEventCfg = "genEvents109.cfg";
  $genOutput = startGenEvents( $genEventCfg );
  waitGenEventsExit();
  sleep $maxTime+15;    # based on a maintenance period of 10s
  $result += stopServer(1);

  # grep for some events in the log
  print "should see 2 reconfigure commands:\n";
  print `cat $dispatcherLog | grep 'reconfigure: received command'`;
  print "should see 10 killing commands in 2 groups (4 and then 6) with max execution times reflecting the max execution times listed above:\n";
  print `cat $dispatcherLog | grep 'checkOverrunningWorkers: killing'`;

  print "testSub109: overall result: $result\n";
  return $result;
} # testSub109


######
# verification of max execution times and the adjustment thereof
sub testSub110
{
  print "testSub110: checking for memory errors\n";
  print "build up valgrind.supp - the suppression list\n";
  print "execute server on its own and insert the relevant suppression entries into valgrind.supp\n";
  print "this gives a baseline to unreleased objects - most unreleased objects are a result of process forking\n";
  print "execute using: 'valgrind --leak-check=full --suppressions=./valgrind.supp --gen-suppressions=all /usr/local/bin/txDelta -L -c test110.cfg 2>&1 | tee memleak.txt'\n";
  print "stop the server with: './test.pl -s'\n";
  print "\nafter completing the above run the server again with:\n";
  print "  'valgrind --leak-check=full --suppressions=./valgrind.supp /usr/local/bin/txDelta -L -c test110.cfg 2>&1 | tee memleak1.txt'\n";
  print "to verify no growth in allocated blocks that are discarded:\n";
  print " cat memleak.txt| grep 'malloc/free: in use'\n cat memleak1.txt| grep 'malloc/free: in use'\n";
  print "to verify that more blocks were in fact allocated (and freed):\n";
  print " cat memleak.txt| grep -P 'malloc/free: [\\d,]+'\n cat memleak1.txt| grep -P 'malloc/free: [\\d,]+'\n";
  print( "hit enter to continue and generate a set of events\n" );
  <>;

  my $result = 0;
  my $genEventCfg = "genEvents110.cfg";
  my $genOutput = startGenEvents( $genEventCfg );
  waitGenEventsExit();
  $result += stopServer(1);

  print "testSub110: overall result: $result\n";
  return $result;
} # testSub110

######
# benchmarking of getMSN query
sub testSub200
{
  print "testSub200: benchmarking of the getMSN query\n";
  my $result = 0;
  my $output;
  my $cfgFile = "test200.cfg";
  my $genEventCfg = "genEvents200.cfg";
  my $queue = 'msnreq';

  #my @workerList = (2,6,20);
  my @workerList = (2,4,6,8,20);
  foreach my $numWorkers (@workerList)
  {
    print "\n\nsetting the workers to $numWorkers\n";
    $output = startServer( $cfgFile );
    sleep 1;
    my $txDeltaCmd = new ServerCommand( @connectDetails );
    my ($retVal,$submitErrorString) = $txDeltaCmd->setNumWorkers( $queue, $numWorkers );
    sleep 1;

    # generate test events
    $startTime = time();
    my $nowString = localtime $startTime;
    print "startTime: $startTime - $nowString\n";
    my $genOutput = startGenEvents( $genEventCfg );
    my $genPid = 0;
    $genPid = $1 if( $genOutput =~ /has pid (\d+)/ );
    if( $genPid == 0 )
    {
      $result++;
      print "testSub200: failed to start the event generator - no pid\n";
      return $result;
    } # if

    # wait for the test event generator to exit
    waitGenEventsExit();
    # wait for the server to stop and exit
    $result += stopServer( 1 );
    printElapsedTime();
    # analyse results
    $result += compareDispatcherProcessedEvents( $dispatcherLog, $genEventCfg, $recoveryLog );
  } # if

  print "testSub200: overall result: $result\n";
  return $result;
} # testSub101


######
# support methods
######

######
# prints the elapsed time
sub printElapsedTime
{
  if( ($startTime==0) || ($endTime==0) )
  {
    warn "printElapsedTime: either startTime:$startTime or endTime:$endTime was never set\n";
    return;
  } # if
  $elapsedTime = $endTime - $startTime;
  my $tmEnd = gmtime( $elapsedTime );
  $tmEnd->hour += 24*$tmEnd->yday if( $tmEnd->yday > 0 );
  print "\n======================================\n";
  printf( "Test took %02d:%02d:%02d\n", $tmEnd->hour, $tmEnd->min, $tmEnd->sec );
  print "======================================\n\n";
} # printElapsedTime

######
# starts the server
sub startServer
{
  my $cfg = shift;
  my $cmd = "$execFile -L -D -c $cfg";
  print( "startServer: $cmd\n" );
  my $output = `$cmd`;
  chomp $output;
  return $output
} # startServer
sub startServerMemCheck
{
  my $cfg = shift;
  my $cmd = "$valgrindExec --leak-check=full --suppressions=valgrind.supp $execFile -L -c $cfg 2>&1 | tee memleak.txt";
  print( "startServerMemCheck: please start the server with - '$cmd'\n" );
  print( "hit enter to continue\n" );
  <>;
#  my $output = `$cmd`;
#  chomp $output;
#  return $output
} # startServer

######
# stops the server
sub stopServer
{
  my $bSendExitOnDone = shift;
  my $result = 0;
  my $output = '';

  if( $bSendExitOnDone )
  {
    $result += cmdShutdown();
    print "stopServer: sent dispatcher a CMD_EXIT_WHEN_DONE - waiting for it to finish\n";
    print "stopServer:\n";

    # wait for dispatcher to exit
    my $bDone = 0;
    my $countSecs = 0;
    my $lastChar = "";
    while( !$bDone )
    {
      my $cmd = "cat $dispatcherLog | grep 'main: bExitOnDone and all queues empty'";
      my $output = `$cmd`;
      if( $output =~ /main: bExitOnDone and all queues empty/ )
      {
        $bDone = 1;
        print "\n" if( $lastChar ne "\n" && (length($lastChar)>0) );
        $endTime = time();
        my $nowString = localtime $endTime;
        print( "endTime:$endTime - $nowString\n" );
      } # if
      else
      {
        print( "." ); $lastChar = ".";
        $countSecs++;
        if( int($countSecs/15)*15 == $countSecs )
        {
          print( "\n" ) ; $lastChar = "\n";
        } # if
        sleep( 1 );
      } # else
    } # while
    sleep( 1 );   # give it time to properly exit
  } # if $bSendExitOnDone
  else
  {
    my $cmd = "kill `cat $pidFile`";
    print( "stopServer: $cmd\n" );
    $output = `$cmd`;
    chomp $output;
    sleep( 2 );
  } # else

  my $cmd = "ps ax | grep '$execFile' | grep -v grep";
  my $killOutput = `$cmd`;
  my ($numLines,$splitOutput) = countLines( $killOutput );
  my $expectedLines = 0;
  if( ($expectedLines-$numLines) != 0 )
  {
    $result++ ;
    print "stopServer: should see $expectedLines processes - counted $numLines instances\ncmd:$cmd\n'${output}'\noverall result: $result\n";
  } # if
  else
  {
    print "stopServer: server stopped\n";
  } # else
  return $result;
} # stopServer

######
# starts the server
sub startGenEvents
{
  my $cfg = shift;
  #my $cmd = "$genExecFile -v -d -c $cfg";
  my $cmd = "$genExecFile -d -c $cfg";
  print( "startGenServer: $cmd\n" );
  system( $cmd );
  print "startGenEvents done\n";
  # for some strange reason the backticks do not allow the code generator to detach properly
  # my $output = `$cmd`;
  my $output = `cat $genExecPidFile`;
  chomp $output;
  return "has pid $output";
} # startGenEvents

######
# stops the server
sub stopGenEvents
{
  my $result = 0;
  my $cmd = "kill `cat $genExecPidFile`";
  print( "stopGenEvents: $cmd\n" );
  my $output = `$cmd`;
  chomp $output;

  sleep( 1 );
  $cmd = "ps ax | grep '$genExecFile' | grep -v grep";
  my $killOutput = `$cmd`;
  my ($numLines,$splitOutput) = countLines( $killOutput );
  my $expectedLines = 0;
  if( ($expectedLines-$numLines) != 0 )
  {
    $result++ ;
    print "stopGenEvents: should see $expectedLines processes - counted $numLines instances\ncmd:$cmd\n${output}overall result: $result\n";
  } # if
  else
  {
    print "stopGenEvents: stopped\n";
  } # else
  return $result;
} # stopGenEvents

######
# wait for test event generator to exit
sub waitGenEventsExit
{
  my $bFinished = 0;
  my $countSecs = 0;
  my $lastChar = "";
  print "waitGenEventsExit:\n";
  while( !$bFinished )
  {
    sleep( 1 );
    my $cmd = "ps ax | grep '$genExecFile' | grep -v grep";
    my $waitOutput = `$cmd`;
    my ($numLines,$splitOutput) = countLines( $waitOutput );
    if( $numLines == 0 )
    {
      $bFinished = 1;
      print "\n" if( $lastChar ne "\n" ); $lastChar = "\n";
      print "waitGenEventsExit: $genExecFile exited\n";
    } # if
    else
    {
      print( "*" ); $lastChar = "*";
      $countSecs++;
      if( int($countSecs/15)*15 == $countSecs )
      {
        print( "\n" ); $lastChar = "\n";
      } # if
    } # else
  } # while
} # waitGenEventsExit

######
# counts the number of lines in the output
sub countLines
{
  my $output = shift;
  my @arr = split /\n/, $output;
  return (scalar(@arr),\@arr);
} # countLines

######
# check main log for errors
sub checkMainLog
{
  my $logName = shift;
  my $result = 0;
  if( !open( LOGFILE, "<", $logName ) )
  {
    warn( "checkMainLog: failed to open logfile '$logName'" );
    return 1;
  } # if
  
  my $lineNo = 0;
  while( <LOGFILE> )
  {
    my $line = $_;
    $lineNo++;
    if( $line =~ /CMD_CHILD_SIGNAL/ )
    {
      $result++;
      chomp( $line );
      print( "checkMainLog CMD_CHILD_SIGNAL $lineNo: '$line'\n" );
    } # if
    elsif( $line =~ /caught exception/ )
    {
      $result++;
      chomp( $line );
      print( "checkMainLog exception $lineNo: '$line'\n" );
    } # if
  } # while

  close( LOGFILE );
  print( "checkMainLog '$logName' result $result\n" );
  return $result;
} # checkMainLog

######
# check the controller log for errors
sub checkControllerLog
{
  my $logName = shift;
  my $result = 0;
  if( !open( LOGFILE, "<", $logName ) )
  {
    warn( "checkControllerLog: failed to open logfile '$logName'" );
    return 1;
  } # if
  
  my $lineNo = 0;
  while( <LOGFILE> )
  {
    my $line = $_;
    $lineNo++;
#    if( $line =~ /CMD_CHILD_SIGNAL/ )
#    {
#      $result++;
#      chomp( $line );
#      print( "checkControllerLog CMD_CHILD_SIGNAL $lineNo: '$line'\n" );
#    } # if
#    elsif( $line =~ // )
#    {
#      $result++;
#      print( "checkControllerLog  $lineNo: '$line'\n" );
#    } # if
  } # while

  close( LOGFILE );
  print( "checkControllerLog '$logName' result $result\n" );
  return $result;
} # checkControllerLog

######
# check the dispatcher log for errors
sub checkDispatcherLog
{
  my $logName = shift;
  my $result = 0;
  if( !open( LOGFILE, "<", $logName ) )
  {
    warn( "checkDispatcherLog: failed to open logfile '$logName'" );
    return 1;
  } # if
  
  my $lineNo = 0;
  while( <LOGFILE> )
  {
    my $line = $_;
    $lineNo++;
    if( $line =~ /respawnChild: child pid \d+ newpid \d+ exit/ )
    {
      $result++;
      chomp( $line );
      print( "checkDispatcherLog respawnChild $lineNo: '$line'\n" );
    } # if
#    elsif( $line =~ // )
#    {
#      $result++;
#      print( "checkDispatcherLog  $lineNo: '$line'\n" );
#    } # if
  } # while

  close( LOGFILE );
  print( "checkDispatcherLog '$logName' result $result\n" );
  return $result;
} # checkDispatcherLog

######
# check the socket log for errors
sub checkSocketLog
{
  my $logName = shift;
  my $result = 0;
  if( !open( LOGFILE, "<", $logName ) )
  {
    warn( "checkSocketLog: failed to open logfile '$logName'" );
    return 1;
  } # if
  
  my $lineNo = 0;
  while( <LOGFILE> )
  {
    my $line = $_;
    $lineNo++;
    if( $line =~ /(failed to find a valid service)/ )
    {
      $result++;
      chomp( $line );
      print( "checkSocketLog port binding failed $lineNo: '$line'\n" );
    } # if
#    elsif( $line =~ // )
#    {
#      $result++;
#      chomp( $line );
#      print( "checkSocketLog  $lineNo: '$line'\n" );
#    } # if
  } # while

  close( LOGFILE );
  print( "checkSocketLog '$logName' result $result\n" );
  return $result;
} # checkSocketLog

######
# gets the running process pid
sub getLogParam
{
  my ($logName,$grepParam,$extractParam) = @_;
  my $cmd = "cat $logName | grep $grepParam";
  my $output = `$cmd`;
  chomp( $output );
  print( "getLogParam: '$cmd' -\n$output\n" ) if( $beVerbose );
  my @pids = ($output =~/$extractParam/g);
  my $pid = $pids[@pids-1];
  return $pid;
} # getLogParam

######
# parses the dispatcher log to extract the queue setup
sub parseDispatcherQueue
{
  my ($logName,$cfgFile) = @_;
  my $result = 0;
  if( !open( LOGFILE, "<", $logName ) )
  {
    warn( "parseDispatcherQueue: failed to open logfile '$logName'" );
    return (1,undef);
  } # if
  
  my $lineNo = 0;
  my %queues;
  while( <LOGFILE> )
  {
    my $line = $_;
    $lineNo++;
    if( $line =~ /(\w+) init: queue '(\w+)', numWorkers (\d+), maxLength (\d+), maxExecTime ([-\d]+)/ )
    {
      my $entry = {name=>$2,type=>$1,numWorkers=>$3,maxLength=>$4,maxExecTime=>$5};
      $entry->{children} = [];
      $queues{$2} = $entry;
    } # if
    elsif( $line =~ /(\w+) createChild: queue '(\w+)' forked child pid (\d+)/ )
    {
      my $children = $queues{$2}->{children};
      push( @$children, $3 );
    } # elsif
  } # while
  close( LOGFILE );

  my ($confQueues,$totWorkers) = parseDispatcherQueueConf( $cfgFile );
  print "\nparseDispatcherQueue: found the following queues in the log:\n";
  $result += printDispatcherQueues( \%queues );
  print "\nparseDispatcherQueue: this should agree with the conf file ($totWorkers workers in total):\n";
  printDispatcherQueues( $confQueues );
  print( "\n" );

  return ($result, \%queues);
} # parseDispatcherQueue

######
# parses the dispatcher log to extract worker log entries - ie which events were processed and how
#
sub parseDispatcherWorkEntries
{
  my ($logName) = @_;
  print "parseDispatcherWorkEntries: parsing log file '$logName'\n";
  my $result = 0;
  if( !open( LOGFILE, "<", $logName ) )
  {
    warn( "parseDispatcherWorkEntries: failed to open logfile '$logName'" );
    return (1,undef,undef,undef,undef,undef,undef,undef,undef,undef,undef);
  } # if
  
  my $lineNo = 0;
  my %workEntries;
  my %maxTimeInQueue;
  my %maxQueueLen;
  my %recoveryEvents;
  my %expiredEvents;
  my %numEventsQueued;
  my %runningTimes;
  # counters based on the outcome of the urlCall or scriptExec log line
  my %successEvents;
  my %failEvents;
  my %totalEvents;
  while( <LOGFILE> )
  {
    my $line = $_;
    $lineNo++;
    if( $line =~ /^\[(\d+-\d+-\d+ \d+:\d+:\d+ \d+).+worker (\d+) main: received event (\w+).*ref: (\d+)\s+queue: '(\w+)'/ )
    {
      my $logRef = $1;
      my $workerPid = $2;
      my $type = $3;
      my $ref = $4;
      my $queue = $5;
      if( !exists( $workEntries{$type} ) )
      {
        $workEntries{$type} = [];
        $runningTimes{$type} = {starttime=>$logRef};
      } # if
      my $entry = {type=>$type,reference=>$ref,queue=>$queue,logref=>$logRef,pid=>$workerPid};
      push( @{$workEntries{$type}}, $entry );
      print "parseDispatcherWorkEntries: type=>$type,reference=>$ref,queue=>$queue,logref=>$logRef,pid=>$workerPid\n" if( $beVerbose );
    } # if
    elsif( $line =~ /^\[\d+-\d+-\d+ \d+:\d+:\d+ \d+.+feedWorker: queue '(\w+)' popped event from queue after (\d+)s: (\w+)/ )
    {
      my $queue = $1;
      my $time = $2;
      if( !exists($maxTimeInQueue{$queue}) )
      {
        $maxTimeInQueue{$queue} = 0;
        $maxQueueLen{$queue} = 0;
        $numEventsQueued{$queue} = 0;
      } # if
      $maxTimeInQueue{$queue} = $time if( $time>$maxTimeInQueue{$queue} );
      $numEventsQueued{$queue}++;
    } # elsif
    elsif( $line =~ /^\[\d+-\d+-\d+ \d+:\d+:\d+ \d+.+queueEvent: queue '(\w+)' qlen (\d+) queued event: (\w+)/ )
    {
      my $queue = $1;
      my $len = $2;
      if( !exists($maxTimeInQueue{$queue}) )
      {
        $maxTimeInQueue{$queue} = 0;
        $maxQueueLen{$queue} = 0;
        $numEventsQueued{$queue} = 0;
      } # if
      $maxQueueLen{$queue} = $len if( $len>$maxQueueLen{$queue} );
    } # elsif
    elsif( $line =~ /^\[\d+-\d+-\d+ \d+:\d+:\d+ \d+.+dumpList: processed (\d+) entries, dumped (\d+) expired events, queue '(\w+)', reason '([^']+)'/ )
    {
      my $numRecoveryEntries = $1;
      my $numExpiredEvents = $2;
      my $queue = $3;
      my $reason = $4;
      if( !exists($recoveryEvents{$queue}) )
      {
        $recoveryEvents{$queue} = [];
        $expiredEvents{$queue} = [];
      } # if
      push @{$recoveryEvents{$queue}}, $numRecoveryEntries;
      push @{$expiredEvents{$queue}}, $numExpiredEvents;
    } # elsif
    elsif( $line =~ /((urlCall)|(scriptExec)) success: (\d) queue: '(\w+)'/ )
    {
      my $queue = $5;
      my $success = $4;
      #print "$1: success: $success queue: '$queue' - $line\n";
      if( !exists($successEvents{$queue}) )
      {
        $successEvents{$queue} = 0;
        $failEvents{$queue} = 0;
        $totalEvents{$queue} = 0;
      } # if
      $totalEvents{$queue}++;
      $successEvents{$queue}++ if($success);
      $failEvents{$queue}++ if(!$success);
    } # elsif
    elsif( $line =~ /main: done processing event (\w+) elapsed time (\d+) done timestamp (\d+-\d+-\d+ \d+:\d+:\d+)/ )
    {
      # capture the last time an event of a particular type executed
      # capture individual event execution times - future
      my $logRef = $3;
      my $type = $1;
      my $execTime = $2;  # not doing anything with this for the moment
      if( exists( $workEntries{$type} ) )
      {
        $runningTimes{$type}->{lasttime} = $logRef;
      } # if
    } # elsif
  } # while
  close( LOGFILE );

  print( "parseDispatcherWorkEntries: found the following entries:\n" );
  if( scalar(keys %workEntries) == 0 )
  {
    print "  none\n";
  } # if
  else
  {
    foreach my $eventType (keys %workEntries)
    {
      my $entries = $workEntries{$eventType};
      my $numEntries = scalar(@$entries);
      print( "  $eventType: $numEntries occurrences\n" );
      if( $beVerbose )
      {
        foreach my $entry (@$entries)
        {
          print "    type=>".$entry->{type}.",reference=>$entry->{reference},queue=>$entry->{queue},logref=>$entry->{logref},pid=>$entry->{pid}\n";
        } # foreach
      } # if
    } # foreach
  } # else

  return ($result,\%workEntries,\%maxTimeInQueue,\%maxQueueLen,\%numEventsQueued,\%runningTimes,\%recoveryEvents,\%expiredEvents,\%successEvents,\%failEvents,\%totalEvents);
} # parseDispatcherWorkEntries

######
# compares the parsed dispatcher log entries and those take from the config files
sub compareDispatcherProcessedEvents
{
  my ($dispatcherLog,$genEventCfg,$recoveryLog) = @_;
  print "compareDispatcherProcessedEvents: dispatcherLog:'$dispatcherLog' genEventCfg:'$genEventCfg' recoveryLog:'$recoveryLog'\n";
  my $result = 0;

  # check the dispatcher log to see what really happened
  my ($res,$workerEntries,$maxTimeInQueue,$maxQueueLen,$numEventsQueued,$runningTimes,$recoveryEvents,$expiredEvents,$successEvents,$failEvents,$totalEvents) = parseDispatcherWorkEntries( $dispatcherLog );
  $result += $res;
  print "compareDispatcherProcessedEvents: result(parseDispatcherWorkEntries): $result\n";

  # parse the genEvents cfg file - this defines the events that were given for processing
  my ($res1,$streams) = parseGenEventsCfg( $genEventCfg );
  $result += $res1;
  print "compareDispatcherProcessedEvents: result(parseGenEventsCfg): $result\n";

  # print additional queue stats
  print "events processed per queue:\n";
  foreach my $queue (keys %$successEvents)
  {
    print "  queue '$queue' totalEvents:$totalEvents->{$queue} success: $successEvents->{$queue} failed:$failEvents->{$queue}\n";
  } # foreach
  print "queue stats:\n";
  foreach my $queue (keys %$maxQueueLen)
  {
    print "  queue '$queue' maxTimeInQueue:$maxTimeInQueue->{$queue}s maxQueueLen:$maxQueueLen->{$queue} numEventsQueued:$numEventsQueued->{$queue}\n";
  } # foreach
  print "queue recovery details:\n";
  my %totRecoveredPerQ;
  my %totExpiredPerQ;
  foreach my $queue (keys %$recoveryEvents)
  {
    my $recovered = $recoveryEvents->{$queue};
    my $expired = $expiredEvents->{$queue};
    my $recoveries = join( ',', @$recovered );
    my $expiries = join( ',', @$expired );
    my $totRecovered = 0;
    my $totExpired = 0;
    foreach my $numRecovered ( @$recovered )
    {
      $totRecovered += $numRecovered;
    } # foreach
    foreach my $numExpired ( @$expired )
    {
      $totExpired += $numExpired;
    } # foreach

    # save the totals to use it later
    $totRecoveredPerQ{$queue} = $totRecovered;
    $totExpiredPerQ{$queue} = $totExpired;
    print "  queue '$queue' recovered $totRecovered ($recoveries), expired $totExpired ($expiries)\n";
  } # foreach

  # check the number of work entries per type and verify the correct queues
  print "summary per event type - numEvents reported as: (numFoundEntries+totRecovered+totExpired=totFound)\n";
  foreach my $stream (@$streams)
  {
    # conf data - what to expect
    my $eventType = $stream->{type};
    my $queue = $stream->{queue};
    my $ref = $stream->{reference};
    chomp( $ref );
    my $foundEntries = $workerEntries->{$eventType};
    my $numEvents = 0;
    $numEvents = $stream->{maxNumEvents} if( exists($stream->{maxNumEvents}) );

    # actual results
    my $numFoundEntries = 0;
    my $foundType = '';
    my $foundQueue = '';
    my $foundRef = '';
    my $eventsPerSec = 0;
    my $secsPerEvent = 0;
    my $totSeconds = 0;
    my $days = 0;
    my $hours = 0;
    my $minutes = 0;
    my $seconds = 0;
    if( ref($foundEntries) eq "ARRAY" )
    {
      $numFoundEntries = scalar(@$foundEntries);
      my $entry = @$foundEntries[0];  # take the info from the first event
      $foundType = $entry->{type};
      $foundQueue = $entry->{queue};
      $foundRef = $entry->{reference};

      my $times = $runningTimes->{$eventType};
      my $firstTime = $times->{starttime};
      my $lastTime = $times->{lasttime};
      my ($fy,$fm,$fd,$fh,$fi,$fs) = ($firstTime =~ /(\d+)-(\d+)-(\d+) (\d+):(\d+):(\d+)/);
      my ($ly,$lm,$ld,$lh,$li,$ls) = ($lastTime  =~ /(\d+)-(\d+)-(\d+) (\d+):(\d+):(\d+)/);
      if( defined($fy) && defined ($ly) )
      {
        ($days,$hours,$minutes,$seconds) = Delta_DHMS( $fy,$fm,$fd,$fh,$fi,$fs,$ly,$lm,$ld,$lh,$li,$ls );
        $totSeconds = $seconds + 60*$minutes + 3600*$hours + 86400*$days;
        #print "days:$days,hours:$hours,minutes:$minutes,seconds:$seconds,totSeconds:$totSeconds $fy,$fm,$fd,$fh,$fi,$fs $ly,$lm,$ld,$lh,$li,$ls\n";
      } # if
      else
      {
        print "compareDispatcherProcessedEvents: failed to parse firstTime ($firstTime) or lastTime ($lastTime) for event '$eventType'\n";
      } # else
    } # if
    else
    {
      print "ERROR compareDispatcherProcessedEvents: found no entries for $eventType\n";
      $result++;
    } # else

    if( ($numFoundEntries > 0) && ($totSeconds>0) )
    {
      $eventsPerSec = $numFoundEntries / $totSeconds;
      $secsPerEvent = $totSeconds / $numFoundEntries;
      $eventsPerSec = sprintf( "%.3f", $eventsPerSec );
      $secsPerEvent = sprintf( "%.3f", $secsPerEvent );
    } # if

    my $totRecovered = 0;
    my $totExpired = 0;
    $totRecovered = $totRecoveredPerQ{$queue} if exists($totRecoveredPerQ{$queue});
    $totExpired = $totExpiredPerQ{$queue} if exists($totExpiredPerQ{$queue});
    my $totFound = $numFoundEntries+$totRecovered+$totExpired;

    print "eventType:$eventType($foundType), queue:$queue($foundQueue), numEvents:$numEvents($numFoundEntries+$totRecovered+$totExpired=$totFound), ref:$ref($foundRef), rate/s:$eventsPerSec s/event:$secsPerEvent elapsed $hours:$minutes:$seconds";
    if( ($numEvents != $totFound) || ($queue ne $foundQueue) || ($ref ne $foundRef) )
    {
      print " - ERROR - number or type of events discrepency expected(found)\n";
      $result++;
    } # if
    else
    {
      print "\n";
    } # else
  } # foreach


  return $result
} # compareDispatcherProcessedEvents

######
# parses genEvents.cfg
sub parseGenEventsCfg
{
  my ($confName) = @_;
  my @streams;

  if( !open( CONFFILE, "<", $confName ) )
  {
    warn( "parseGenEventsCfg: failed to open logfile '$confName'" );
    return (1,undef);
  } # if
  print "parseGenEventsCfg: parsing config file '$confName'\n";
  
  my $lineNo = 0;
  my $section = '';
  while( <CONFFILE> )
  {
    my $line = $_;
    $lineNo++;
    if( $line =~ /^\[(\w+)]/ )
    {
      $section = $1;
      print "parseGenEventsCfg: processing section '$section'\n";
    } # if
    elsif( $section eq "main" )
    {
      # ignore the section
    } # elsif
    elsif( $section =~ /^stream(\d+)/ )
    {
      my $streamIndex = $1;
      if( $line =~ /^\s*(\w+)\s*=\s*(.+)/ )
      {
        $streams[$streamIndex]{$1} = $2;
      } # if
      else
      {
        chomp( $line );
        warn "parseGenEventsCfg: unable to parse line $lineNo section $section: '$line'\n" if( ( length($line) > 0 ) && ($line !~ /^\s*#/));
      } # else
    } # if
  } # while
  close( CONFFILE );
  return (0, \@streams);
} # parseGenEventsCfg

######
# parses the queues from the config file
sub parseDispatcherQueueConf
{
  my ($cfgFile) = @_;
  my $countWorkers = 0;
  my $cmd = "cat $cfgFile | grep queue";
  my $output = `$cmd`;

  my $queueNo = 0;
  my $bFound = 1;
  my %queues;
  while( $bFound )
  {
    my $keyName = "queue$queueNo\.name";
    my $keyNumWorkers = "queue$queueNo\.numWorkers";
    my $keyMaxLength = "queue$queueNo\.maxLength";
    my $keyType = "queue$queueNo\.type";
    my $keyMaxExecTime = "queue$queueNo\.maxExecTime";

    my $name;
    my $numWorkers = 0;
    my $maxLength = 0;
    my $type = 'eventQueue';
    my $maxExecTime = 0;
    $name = $1 if( $output =~ /$keyName\s*=\s*([\d\w]+)/s );
    $numWorkers = $1 if( $output =~ /$keyNumWorkers\s*=\s*(\d+)/s );
    $maxLength = $1 if( $output =~ /$keyMaxLength\s*=\s*(\d+)/s );
    $type = $1 if( $output =~ /$keyType\s*=\s*(\w+)/s );
    $maxExecTime = $1 if( $output =~ /$keyMaxExecTime\s*=\s*([-\d]+)/s );
    if( defined($name) && (length($name)>0) )
    {
      my $entry = {name=>$name,type=>$type,numWorkers=>$numWorkers,maxLength=>$maxLength,maxExecTime=>$maxExecTime};
      $queues{$name} = $entry;
      $countWorkers += $numWorkers;

      $bFound = 1;
      $queueNo++;
    } # if
    else
    {
      $bFound = 0;
    } # else
  } # while

  return (\%queues,$countWorkers);
} # parseDispatcherQueueConf

######
sub printDispatcherQueues
{
  my $queues = shift;
  my $result = 0;
  foreach my $key (keys %$queues)
  {
    my $entry = $queues->{$key};
    my $childList;
    my $comment = '';
    if( exists( $entry->{children} ) )
    {
      my $children = $entry->{children};
      $childList = join ",",@$children;
      my $numChildren = @$children;
      if( $numChildren != $entry->{numWorkers} )
      {
        $result++;
        $comment .= ' - num children NOT correct!';
      } # if
    } # if exists
    my $prettyQueueName = substr( "'$entry->{name}'         ", 0, 12 );
    print "queue $prettyQueueName, type $entry->{type}, numWorkers $entry->{numWorkers}, maxLength $entry->{maxLength}, maxExecTime $entry->{maxExecTime}";
    print ", pids $childList $comment" if( defined($childList) );
    print "\n";
  } # foreach
  return $result;
} # printDispatcherQueues

######
# tails a log and waits for a specific line
sub tailLogFor
{
  my ($logName,$searchTerm,$maxWaitTime) = @_;
  my $timeToDie = 0;
  my $seekResult = 1;
  my $bFound = 0;
  my $startTime = time;
  my $countSecs = 0;
  my $lastChar = "";
  my @results;

  my $openResult = open( LOGFILE, "<", $logName );
  if( !$openResult )
  {
    warn "tailLogFor: cannot open logfile: $!";
    return (1,undef);
  } # if
  print "tailLogFor: log: '$logName' regex: '$searchTerm' maxWaitTime:$maxWaitTime\n";

  while( !$timeToDie && $seekResult && !$bFound )
  {
    my $line;
    while( defined($line=<LOGFILE>) && !$bFound )
    {
      if( @results = ($line =~ /$searchTerm/) )
      {
        $bFound = 1;
        print "\n" if( $lastChar ne "\n" ); $lastChar = "\n";
        chomp $line;
        print "tailLogFor: '$line'\n";
      } # if
    } # while

    if( (time-$startTime) > $maxWaitTime )
    {
      $timeToDie = 1;
      print "\n" if( $lastChar ne "\n" ); $lastChar = "\n";
      print "tailLogFor: failed to find search term\n";
    } # if
    else
    {
      print "o"; $lastChar = "o";
      $countSecs++;
      if( int($countSecs/15)*15 == $countSecs )
      {
        print( "$countSecs\n" ); $lastChar = "\n";
      } # if
      sleep 1;
    } # else
    $seekResult = seek( LOGFILE, 0, 1 );
    warn "tailLogFor: seek failed\n" if( !$seekResult );
  } # while

  print "\n" if( $lastChar ne "\n" );
  close LOGFILE;
  return (!$bFound,\@results);
} # tailLogFor

######
# sends a CMD_SHUTDOWN to the server
sub cmdShutdown
{
  my $txDeltaCmd = new ServerCommand( @connectDetails );
  my ($retVal,$submitErrorString) = $txDeltaCmd->shutdown();

  if( !$retVal )
  {
    print "cmdShutdown: failed to send packet: '$submitErrorString'\n";
    return 1;
  } # if
  return 0;
} # sub cmdShutdown

