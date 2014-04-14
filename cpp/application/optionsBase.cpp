/** @class optionsBase
 optionsBase - base system config class
 
 $Id: optionsBase.cpp 2855 2013-04-19 15:15:24Z gerhardus $
 Versioning: a.b.c a is a major release, b represents changes or new features, c represents bug fixes. 
 @version 1.0.0		18/01/2012		Gerhardus Muller		Script created
 @version 1.1.0		23/06/2012		Gerhardus Muller		Added main.bDropPermPriviledges
 @version 1.2.0		10/10/2012		Gerhardus Muller		fixed txProcSocketLocalPath to be PID dependent
 @version 1.3.0		19/04/2013		Gerhardus Muller		added a '.' to txProcSocketLocalPath so that it creates hidden files

 @note

 @todo
 
 @bug

 Copyright Gerhardus Muller
 */

#include "buildno.h"
#include "buildtime.h"
#include "application/optionsBase.h"
#include "exception/Exception.h"
#include <fstream>
#include <iostream>

/**
 Construction
 */
optionsBase::optionsBase( const char* objName, const char* theAppName )
  : appBaseName( theAppName ),
  log( logger::getInstance( objName ) )
{
}	// optionsBase

/**
 Destruction
 */
optionsBase::~optionsBase()
{
  delete &log;
}	// ~optionsBase

/**
 Standard logging call - produces a generic text version of the optionsBase.
 Memory allocation / deleting is handled by this optionsBase.
 @return pointer to a string describing the state of the optionsBase.  The string has an unspecified lifetime and is not multi-thread safe
 */
std::string optionsBase::toString( )
{
  std::ostringstream oss;
  oss << typeid(*this).name() << ":" << this;
	return oss.str();
}	// toString

/**
 * parses the command line optionsBase and config file "xxx.cfg"
 * only the master process can see command line optionsBase
 * @param ac
 * @param av[]
 * @return false on error or command line handled
 * **/
bool optionsBase::parseOptions( int ac, char* av[] )
{
  try 
  {
    bLogConsole = false;
    bLogStderr = false;
    bFlushLogs = false;
    baseDir = "/usr/local/etc";
    configFile = baseDir + "/";
    configFile.append( appBaseName );
    configFile.append( ".cfg" );
    logBaseDir = "/var/log/";
    logBaseDir.append( "txProc" );
    logBaseDir.append( "/" );
    logFile = appBaseName;
    logFile.append( ".log");
    txProcSocketPath = "txProc.sock";
    txProcSocketLocalPath = ".";
    txProcSocketLocalPath.append( appBaseName );
    char tmp[64];
    sprintf( tmp, ".%d.txProc.sock", getpid() );
    txProcSocketLocalPath.append( tmp );
    buildNo = buildno;
    buildTime = buildtime;
    
    // options that control - typically global options
    // precede long options by a '--'
    po::options_description serverSwitches("switches");
    serverSwitches.add_options()
      ("version,V", "print version string")
      ("help,h", "produce help message")    
      ("display_options,d", "display parsed options")
      ("nologconsole,L", "do not log to stdout")
      ("logstderr,s", "log to stderr - takes preference over logging to console")
      ("flushlogs,f", "flush logs after every write")
      ;
    
    po::options_description mainOptions("[main] optionsBase", DEF_OUTPUT_WIDTH);
    mainOptions.add_options()
      ("main.configFile,c", po::value<std::string>(&configFile)->default_value(configFile.c_str()), "config file")
      ("main.runAsUser", po::value<std::string>(&runAsUser)->default_value("uucp"), "run as user")
      ("main.statsUrl", po::value<std::string>(&statsUrl), "Url for reporting stats")
      ("main.statsQueue", po::value<std::string>(&statsQueue)->default_value("default"), "queue for stats event processing")
      ("main.abortEventQueue", po::value<std::string>(&abortEventQueue)->default_value("mediaManager"), "queue to send abort events to")
      ("main.applicationEventQueue", po::value<std::string>(&applicationEventQueue), "queue to send application events to - leave empty to suppress")
      ("main.applicationEventScript", po::value<std::string>(&applicationEventScript), "standard application to invoke to handle application events")
      ("main.appBaseName", po::value<std::string>(&appBaseName)->default_value(appBaseName.c_str()), "application base name - specify as a command line option to change logfile and socket names by default")
      ("main.logFile", po::value<std::string>(&logFile)->default_value(logFile), "log file - created in main.logBaseDir by default")
      ("main.defaultLogLevel", po::value<int>(&defaultLogLevel)->default_value(5), "log levels 1-10 - only levels less or equal to this will be logged")
      ("main.logGroup", po::value<std::string>(&logGroup)->default_value("users"), "group for log file owner")
      ("main.txProcSocketPath", po::value<std::string>(&txProcSocketPath)->default_value(txProcSocketPath), "Unix domain socket path for txProc (parent)")
      ("main.txProcSocketLocalPath", po::value<std::string>(&txProcSocketLocalPath)->default_value(txProcSocketLocalPath), "Unix domain socket path for txProc - local path")
      ("main.maintInterval", po::value<int>(&maintInterval)->default_value( 10 ), "timer interval to use for background maintenance jobs - 0 to disable")
      ("main.maxShutdownWaitTime", po::value<int>(&maxShutdownWaitTime)->default_value( 10 ), "max time to wait for shutdown")
      ("main.bDropPermPriviledges", po::value<int>((int*)(&bDropPermPriviledges))->default_value( 1 ), "set to 0 for the framework to not drop priviledges permanently - required if queueN.bRunPriviledged=1 is specified")
      ;

    char label[128];
    sprintf( label, "[%s] options", appBaseName.c_str() );
    po::options_description appOptions(label, DEF_OUTPUT_WIDTH);
    addAppOptions( appOptions );

    po::options_description cmdline_options;
    cmdline_options.add(serverSwitches).add(mainOptions).add(appOptions);

    po::options_description config_file_options;
    config_file_options.add(serverSwitches).add(mainOptions).add(appOptions);
    
    if( ac > 0 )
    {
      po::parsed_options parsed = po::command_line_parser(ac, av).options(cmdline_options).allow_unregistered().run();
      store( parsed, vm );
      if( vm.count("flushlogs") ) bFlushLogs = true;
      if( vm.count("main.appBaseName") )
      {
        // redo the constuction of names in case the application name was changed
        configFile = baseDir + "/";
        configFile.append( appBaseName );
        configFile.append( ".cfg" );
        logBaseDir = "/var/log/";
        logBaseDir.append( "txProc" );
        logBaseDir.append( "/" );
        logFile = appBaseName;
        logFile.append( ".log");
        txProcSocketPath = "txProc.sock";
        txProcSocketLocalPath = appBaseName;
        txProcSocketLocalPath.append( ".txProc.sock" );
      } // if
      notify( vm ); // store the optionsBase as it could change the configFile source
    }

    std::ifstream ifs( configFile.c_str() );
    if( ifs.good() )
    {
      po::parsed_options parsed = po::parse_config_file(ifs, config_file_options, true);
      store( parsed, vm);
      notify( vm );
    } // if
    else
      log.warn( log.LOGALWAYS, "parseOptions: no config file '%s'", configFile.c_str() );
    

    // build the full log file names if required
    if( logFile.find( '/' ) == std::string::npos ) logFile = logBaseDir + logFile;
    // build the txProc socket path if required
    if( txProcSocketPath.find( '/' ) == std::string::npos ) txProcSocketPath = logBaseDir + txProcSocketPath;
    if( txProcSocketLocalPath.find( '/' ) == std::string::npos ) txProcSocketLocalPath = logBaseDir + txProcSocketLocalPath;

    // switches
    if( vm.count("nologconsole") ) bLogConsole = false;
    if( vm.count("logstderr") ) bLogStderr = true;
    if( vm.count("flushlogs") ) bFlushLogs = true;
    
    if( vm.count("help") ) 
    {
      std::cout << "\noptionsBase\n" << "==================\n";
      std::cout << serverSwitches << mainOptions << appOptions << "\n\n";
      return false;
    }

    if( vm.count("version") ) 
    {
      std::cout << appBaseName << " - build no " << buildno << " build time " << buildtime << std::endl;
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
    std::cerr << "parseOptions exception " << e.what() << "\n";
    log.error( "parseOptions exception %s\n", e.what() );
    return false;
  } // catch
  catch(Exception e)
  {
    std::cerr << "parseOptions exception " << e.getMessage() << "\n";
    return false;
  } // catch
  return true;
} // parseOptions

/**
 * logs the parsed optionsBase
 * **/
void optionsBase::logOptions( )
{
  std::string optionsBase = displayOptions();
  log.info( log.LOGALWAYS ) << "Parsed optionsBase:\n" << optionsBase;
} // logOptions

/** 
 * displays the parsed optionsBase
 * **/
std::string optionsBase::displayOptions( )
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
