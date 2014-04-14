/** @class optionsNucleus
 optionsNucleus - system config class for nucleus
 
 $Id: optionsNucleus.cpp 2622 2012-10-11 15:24:56Z gerhardus $
 Versioning: a.b.c a is a major release, b represents changes or new features, c represents bug fixes. 
 @version 1.0.0		22/09/2009		Gerhardus Muller		Script created

 @note

 @todo
 
 @bug

	Copyright Notice
 */

#include "nucleus/optionsNucleus.h"
#include "options.h"
#include <iostream>
#include <fstream>

/**
 Construction
 */
optionsNucleus::optionsNucleus( )
  : log( logger::getInstance( "optionsNucleus" ) )
{
}	// optionsNucleus

/**
 Destruction
 */
optionsNucleus::~optionsNucleus()
{
  delete &log;
}	// ~optionsNucleus

/**
 Standard logging call - produces a generic text version of the options.
 Memory allocation / deleting is handled by this options.
 @return pointer to a string describing the state of the options.  The string has an unspecified lifetime and is not multi-thread safe
 */
std::string optionsNucleus::toString( )
{
  std::ostringstream oss;
  oss << typeid(*this).name() << ":" << this;
	return oss.str();
}	// toString

/**
 * parses the command line options and config file
 * only the master process can see command line options
 * @param ac
 * @param av[]
 * @return false on error or command line handled
 * **/
bool optionsNucleus::parseOptions( int ac, char* av[] )
{
  try 
  {
    bLogConsole = true;
    bLogStderr = false;
    bFlushLogs = false;
    configFile = pOptions->configFile;
    logFile = pOptions->logBaseDir;
    logFile.append( "nucleus.log" );
    statsDir = pOptions->logBaseDir;
    statsDir.append( "stats/" );
    unixSocketPath = pOptions->logBaseDir;
    unixSocketPath.append( pOptions->APP_BASE_NAME );
    unixSocketPath.append( ".sock" );
    unixSocketStreamPath = pOptions->logBaseDir;
    unixSocketStreamPath.append( pOptions->APP_BASE_NAME );
    unixSocketStreamPath.append( "Stream.sock" );

    // options that control - typically global options
    // precede long options by a '--'
    po::options_description serverSwitches("switches");
    serverSwitches.add_options()
      ("help,h", "produce help message")    
      ("display_options,d", "display parsed options")
      ("nologconsole,L", "do not log to stdout")
      ("logstderr,s", "log to stderr - takes preference over logging to console")
      ("flushlogs,f", "flush logs after every write")
      ;

    po::options_description serverOptions("[main] options", DEF_OUTPUT_WIDTH);
    serverOptions.add_options()
      ("main.configFile,c", po::value<std::string>(&configFile)->default_value(pOptions->configFile), "config file")
      ("main.runAsUser", po::value<std::string>(&runAsUser)->default_value("uucp"), "run as user")
      ("main.statsUrl", po::value<std::string>(&statsUrl), "Url for reporting stats")
      ("main.statsQueue", po::value<std::string>(&statsQueue)->default_value("default"), "queue for stats event processing")
      ;

    // nucleus options
    po::options_description nucleusOptions("[nucleus] options", DEF_OUTPUT_WIDTH);
    nucleusOptions.add_options()
      ("nucleus.logFile", po::value<std::string>(&logFile)->default_value(logFile), "log file - created in main.logBaseDir by default")
      ("nucleus.defaultLogLevel", po::value<int>(&defaultLogLevel)->default_value(5), "log levels 1-10 - only levels less or equal to this will be logged")
      ("nucleus.logGroup", po::value<std::string>(&logGroup)->default_value("uucp"), "group for log file")
      ("nucleus.maintInterval", po::value<unsigned int>(&maintInterval)->default_value( 10 ), "timer interval used for maintenance, 0 disables, base timer for event expiration, max exec times and the delay queue")
      ("nucleus.expiredEventInterval", po::value<unsigned int>(&expiredEventInterval)->default_value( 10 ), "interval in seconds between checks for expired events in the queue, 0 disables")
      ("nucleus.bLogQueueStatus", po::value<unsigned int>(&bLogQueueStatus)->default_value( 1 ), "logs queue status on maintenance interval (1 to enable, 0 to disable")
      ("nucleus.maxNumQueues", po::value<unsigned int>(&maxNumQueues)->default_value( 100 ), "max number of queues to provision for")
      ("nucleus.statsDir", po::value<std::string>(&statsDir)->default_value(statsDir), "stats directory for the queues")
      ("nucleus.activeQueues", po::value<std::string>(&activeQueues), "comma separated list of active queue names")
      ("nucleus.notLocalqueueRouterQueue", po::value<std::string>(&notLocalqueueRouterQueue), "queue that handles events destined for remote nodes")
      ("nucleus.unixSocketPath", po::value<std::string>(&unixSocketPath)->default_value(unixSocketPath), "unix socket path to submit events to the dispatcher from outside")
      ("nucleus.unixSocketStreamPath", po::value<std::string>(&unixSocketStreamPath)->default_value(unixSocketStreamPath), "unix socket path to submit events to the dispatcher from outside - stream connection")
      ("nucleus.socketGroup", po::value<std::string>(&socketGroup)->default_value("uucp"), "group for Unix domain socket")
      ("nucleus.maxNetworkDescriptors", po::value<unsigned int>(&maxNetworkDescriptors)->default_value( 300 ), "indication of the maximum num of descriptors in the epoll object")
       ;
    
//    // queue options - think this is necessary otherwise it does not recognise it even as unparsed values
//    po::options_description queuesOptions("[queues] options", DEF_OUTPUT_WIDTH);
//    queuesOptions.add_options()

     // worker options
    po::options_description workerOptions("[worker] options", DEF_OUTPUT_WIDTH);
    workerOptions.add_options()
      ("worker.perlPath", po::value<std::string>(&perlPath)->default_value( "/usr/bin/perl" ), "Default Perl interpreter")
      ("worker.shellPath", po::value<std::string>(&shellPath)->default_value( "/bin/sh" ), "Default shell to use")
      ("worker.execSuccess", po::value<std::string>(&execSuccess)->default_value( "result:phpProcessedSuccess" ), "Indication of success for scripts")
      ("worker.execFailure", po::value<std::string>(&execFailure)->default_value( "result:phpProcessFailed" ), "Indication of failure for scripts")
      ("worker.urlSuccess", po::value<std::string>(&urlSuccess)->default_value( "result:phpProcessedSuccess" ), "Indication of success for urls")
      ("worker.urlFailure", po::value<std::string>(&urlFailure)->default_value( "result:phpProcessFailed" ), "Indication of failure for urls")
      ("worker.errorPrefix", po::value<std::string>(&errorPrefix)->default_value( "error:" ), "Prefix for an error string in the result")
      ("worker.tracePrefix", po::value<std::string>(&tracePrefix)->default_value( "tracetimestamp:" ), "Prefix for a trace timestamp string in the result")
      ("worker.paramPrefix", po::value<std::string>(&paramPrefix)->default_value( "systemparam:" ), "Prefix for a system parameter in the result")
      ("worker.debugLevel", po::value<int>(&workerDebugLevel)->default_value(5), "debug levels 1-10 - only levels less than this will be logged")
      ("worker.maxGetRequestLength", po::value<unsigned int>(&maxGetRequestLength)->default_value(3800), "requests longer than this use POSTs")
      ("worker.persistentAppRespawnDelay", po::value<unsigned int>(&persistentAppRespawnDelay)->default_value(1), "delay in seconds after a persistent app has exited before it is respawned")
      ("worker.rlimitAs", po::value<unsigned int>(&rlimitAs)->default_value(0), "the maximum size of the process's virtual memory (address space) in bytes")
      ("worker.rlimitCpu", po::value<unsigned int>(&rlimitCpu)->default_value(0), "CPU time limit in seconds. When the process reaches the soft limit, it is sent a SIGXCPU signal")
      ("worker.rlimitData", po::value<unsigned int>(&rlimitData)->default_value(0), "the maximum size of the process's data segment (initialized data, uninitialized data, and heap)")
      ("worker.rlimitFsize", po::value<unsigned int>(&rlimitFsize)->default_value(0), "the maximum size of files that the process may create")
      ("worker.rlimitStack", po::value<unsigned int>(&rlimitStack)->default_value(0), "the maximum size of the process stack, in bytes")
      ;

    po::options_description cmdline_options;
    cmdline_options.add( serverSwitches ).add( serverOptions ).add( nucleusOptions ).add( workerOptions );

    po::options_description config_file_options;
    config_file_options.add( serverSwitches ).add( serverOptions ).add( nucleusOptions ).add( workerOptions );
    
    if( ac > 0 )
    {
      po::parsed_options parsed = po::command_line_parser(ac, av).options(cmdline_options).allow_unregistered().run();
      store( parsed, vm );
      notify( vm ); // store the options as it could change the configFile source
    }

    std::ifstream ifs( configFile.c_str() );
    if( ifs.good() )
    {
      po::parsed_options parsed = po::parse_config_file(ifs, config_file_options, true);
      store( parsed, vm);
      notify( vm );

      // extract all the unknown options relating to queue config and store
      for( unsigned int i = 0; i < parsed.options.size(); i++ )
      {
        po::basic_option<char> p = parsed.options[i];
        if( p.string_key.compare( 0, 7, "queues." ) == 0 )
        {
          queueOptionMap.insert( std::pair<std::string,std::string>(p.string_key,p.value[0]));
          log.debug( log.LOGNORMAL ) << "key: " << p.string_key << " value: '" << p.value[0] << "'";
        } // if
      } // for
    } // if
    else 
      log.warn( log.LOGALWAYS, "parseOptions: no config file '%s'", configFile.c_str() );

    // build the full log file names if required
    if( logFile.find( '/' ) == std::string::npos ) logFile = pOptions->logBaseDir + logFile;

    // switches
    if( vm.count("nologconsole") ) bLogConsole = false;
    if( vm.count("logstderr") ) bLogStderr = true;
    if( vm.count("flushlogs") ) bFlushLogs = true;

    if( vm.count("help") ) 
    {
      std::cout << "\nNucleus options\n" << "==================\n";
      std::cout << serverSwitches << serverOptions << nucleusOptions << workerOptions << "\n";
      std::cout << "Queues are defined in their own section '[queues]' and should be defined as '[queues.qname].'\n";
      std::cout << "nucleus.activeQueues contains a comma separated list of qname's to be started\n";
      std::cout << "Required parameters are name (queues.qname.name - in most cases 'qname' and 'name' would be the same) and numWorkers.\n";
      std::cout << "type('straight','collection'),maxLength,maxExecTime(0),persistentApp(none),parseResponseForObject(1),bRunPriviledged(0),bBlockingWorkerSocket(0),errorQueue(none) are optional\n";
      std::cout << "defaultScript(empty) used if no script is supplied with the event\n";
      std::cout << "defaultUrl(empty) used if no url is supplied with the event\n";
      std::cout << "managementQueue(empty) queue for management events - disabled if empty\n";
      std::cout << "managementEventType(EV_PERL) type of management event to generate. can be any of EV_SCRIPT,EV_PERL,EV_BIN,EV_URL as strings\n";
      std::cout << "managementEvents(QMAN_NONE) to receive. comma separated list of QMAN_PSTARTUP,QMAN_DONE,QMAN_PDIED,QMAN_WSTARTUP\n";
      std::cout << "\n";
      return false;
    }
    if( vm.count("display_options") ) 
    {
      std::cout << displayOptions( );
      return false;
    }
  } // try
  catch(std::exception& e)
  {
    std::cerr << "parseOptions exception " << e.what() << "\n\n";
    log.error( "parseOptions exception %s\n", e.what() );
    return false;
  } // catch
  return true;
} // parseOptions

/**
 * checks if the unknown option exists - this does not check the main value map table
 * as all of the known config values are mapped to class properties
 * see "Allowing Unknown Options" in the docs
 * @return true if it exists
 * **/
bool optionsNucleus::existVar( const char* name )
{
  std::map<std::string,std::string>::iterator it;
  it = queueOptionMap.find( std::string( name ) );
  if( it == queueOptionMap.end() ) 
    return false;
  else
    return true;
} // existVar

/**
 * returns a named option from queueOptionMap
 * please confirm that the variable exists existVar() and isString()
 * @return true if the parameter exists
 * @param name
 * @param value - the config value if it exists - can be empty
 * **/
bool optionsNucleus::getAsString( const char* name, std::string& value )
{
  std::map<std::string,std::string>::iterator it;
  it = queueOptionMap.find( std::string( name ) );
  if( it != queueOptionMap.end() ) 
  {
    value = it->second;
    log.info( log.LOGNORMAL, "getAsString: name:'%s' value:'%s'", name, value.c_str() );
    return true;
  } // if
  else
  {
    log.info( log.LOGNORMAL, "getAsString: name:'%s' no value", name );
    return false;
  } // else
} // getAsString
/**
 * returns a named option from queueOptionMap
 * @return the integer value or the default value if the value does not exist
 * or is not the correct type
 * @param name
 * @param defaultVal
 * **/
int optionsNucleus::getAsInt( const char* name, int defaultVal )
{
  int intVal = defaultVal;
  std::string strVal;
  bool bExists = getAsString( name, strVal );
  if( bExists )
    intVal = atoi( strVal.c_str() );

  return intVal;
} // getAsInt

/**
 * logs the parsed options
 * **/
void optionsNucleus::logOptions( )
{
  log.info( log.LOGNORMAL ) << "Parsed options: " << displayOptions();
} // logOptions

/** 
 * displays the parsed options
 * **/
std::string optionsNucleus::displayOptions( )
{
  std::ostringstream oss;
  std::map<std::string, po::variable_value>::iterator p;
  for( p = vm.begin(); p != vm.end(); p++ )
  {
    oss << p->first << " = ";
    boost::any val = p->second.value();
    if( val.type() == typeid( std::string ) )
      oss << boost::any_cast<std::string>(val);
    else if( val.type() == typeid( int ) )
      oss << boost::any_cast<int>(val);
    else if( val.type() == typeid( unsigned int ) )
      oss << boost::any_cast<unsigned int>(val);
    else if( val.type() == typeid( bool ) )
      oss << (boost::any_cast<unsigned int>(val) ? "true" : "false");
    else
      oss << " Unsupported type: " << val.type().name();
    oss << std::endl;
  } // for

	return oss.str();
} // displayOptions
