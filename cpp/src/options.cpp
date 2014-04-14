/** @class options
 options - system config class
 
 $Id: options.cpp 2483 2012-07-25 08:31:58Z gerhardus $
 Versioning: a.b.c a is a major release, b represents changes or new features, c represents bug fixes. 
 @version 1.0.0		10/11/2009		Gerhardus Muller		Script created
 @version 1.1.0		20/03/2012		Gerhardus Muller		Added the max data gram size to the version string
 @version 1.2.0		25/07/2012		Gerhardus Muller		added buildtime/buildno

 @note

 @todo
 
 @bug

	Copyright Notice
 */

#include "../buildno.h"
#include "../buildtime.h"
#include "../src/options.h"
#include "nucleus/optionsNucleus.h"
#include "networkIf/optionsNetworkIf.h"
#include "exception/Exception.h"
#include "utils/unixSocket.h"
#include <fstream>
#include <iostream>

const char* options::APP_BASE_NAME = "txProc";

/**
 Construction
 */
options::options( )
  : log( logger::getInstance( "options" ) )
{
}	// options

/**
 Destruction
 */
options::~options()
{
  log.debug( log.LOGMOSTLY, "options::~options" );
  delete &log;
}	// ~options

/**
 Standard logging call - produces a generic text version of the options.
 Memory allocation / deleting is handled by this options.
 @return pointer to a string describing the state of the options.  The string has an unspecified lifetime and is not multi-thread safe
 */
std::string options::toString( )
{
  std::ostringstream oss;
  oss << typeid(*this).name() << ":" << this;
	return oss.str();
}	// toString

/**
 * parses the command line options and config file "mserver.cfg"
 * only the master process can see command line options
 * @param ac
 * @param av[]
 * @return false on error or command line handled
 * **/
bool options::parseOptions( int ac, char* av[] )
{
  try 
  {
    argc = ac;
    argv = av;
    baseDir = "/usr/local/etc";
    configFile = baseDir + "/";
    configFile.append( APP_BASE_NAME );
    configFile.append( ".cfg" );
    logBaseDir = "/var/log/";
    logBaseDir.append( APP_BASE_NAME );
    pidName = "/var/run/";
    pidName.append( APP_BASE_NAME );
    pidName.append( ".pid" );
    logFile = APP_BASE_NAME;
    logFile.append( ".log");
    logrotateScript = "/etc/logrotate.d/";
    logrotateScript.append( APP_BASE_NAME );
    bDaemon = false;
    bLogConsole = true;
    bLogStderr = false;
    bFlushLogs = false;
    bStartNucleus = true;
    bStartSocket = true;
    bNoRotate = true;
    buildNo = buildno;
    buildTime = buildtime;
    
    // options that control - typically global options
    // precede long options by a '--'
    po::options_description serverSwitches("switches");
    serverSwitches.add_options()
      ("version,V", "print version string")
      ("daemonise,D","run as a daemon")
      ("help,h", "produce help message")    
      ("display_options,d", "display parsed options")
      ("rotate,r", "rotate logs on startup")
      ("nologconsole,L", "do not log to stdout")
      ("logstderr,s", "log to stderr - takes preference over logging to console")
      ("flushlogs,f", "flush logs after every write")
      ;
    
    po::options_description serverOptions("[main] options", DEF_OUTPUT_WIDTH);
    serverOptions.add_options()
      ("main.configFile,c", po::value<std::string>(&configFile)->default_value(configFile.c_str()), "config file")
      ("main.runAsUser", po::value<std::string>(&runAsUser)->default_value("uucp"), "run as user")
      ("main.pidName", po::value<std::string>(&pidName)->default_value(pidName), "full name for the pid file")
      ("main.logFile", po::value<std::string>(&logFile)->default_value(logFile), "main log file - created in main.logBaseDir by default")
      ("main.logrotatePath", po::value<std::string>(&logrotatePath)->default_value("/usr/sbin/logrotate"), "logrotate executable")
      ("main.logrotateScript", po::value<std::string>(&logrotateScript)->default_value(logrotateScript.c_str()), "logrotate script - normally in /etc/logrotate.d/")
      ("main.logFilesToKeep", po::value<int>(&logFilesToKeep)->default_value(20), "value of rotate parameter for logrotate")
      ("main.defaultLogLevel", po::value<int>(&defaultLogLevel)->default_value(5), "log levels 1-10 - only levels less or equal to this will be logged")
      ("main.recover,r", po::value<std::string>(&recoverFile), "file to recover - does not start up the controller")
      ("main.logBaseDir", po::value<std::string>(&logBaseDir)->default_value( logBaseDir.c_str() ), "logging base directory")
      ("main.statsUrl", po::value<std::string>(&statsUrl), "Url for reporting stats")
      ("main.statsInterval", po::value<int>(&statsInterval)->default_value(180), "stats interval in seconds or 0 to suppress")
      ("main.statsHourStart", po::value<int>(&statsHourStart)->default_value(0), "statsHourStart")
      ("main.statsHourStop", po::value<int>(&statsHourStop)->default_value(24), "statsHourStop")
      ("main.statsChildrenAddress", po::value<std::string>(&statsChildrenAddress), "comma separated list of children slaved to this server for stats purposes - can be either server names or IP addresses, leave blank to disable")
      ("main.statsChildrenService", po::value<std::string>(&statsChildrenService)->default_value("mserver"), "children stats service - either a /etc/service entry or a port number")
      ("main.defaultQueue", po::value<std::string>(&defaultQueue)->default_value("default"), "default queue for event processing")
      ("main.nonucleus", "Do not start the nucleus")
      ("main.nosocket", "Do not start the socket process")
      ("networkIf.packetSuccessReply", po::value<std::string>(&packetSuccessReply)->default_value("phpProcessedSuccess"), "reply written as result:value on a socket as a result of processing (dispatching) successfully")
      ("networkIf.packetFailReply", po::value<std::string>(&packetFailReply)->default_value("phpProcessFailed"), "reply written as result:value on a socket as a result of failing to process (dispatch) successfully - typically not deserialised properly")
      ;


    po::options_description cmdline_options;
    cmdline_options.add( serverSwitches ).add( serverOptions );

    po::options_description config_file_options;
    config_file_options.add( serverSwitches ).add( serverOptions );
    
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
    } // if
    else
      log.warn( log.LOGALWAYS, "parseOptions: no config file '%s'", configFile.c_str() );
      
    // build the full log file names
    if( !logBaseDir.empty() && (logBaseDir[logBaseDir.size()-1] != '/') ) logBaseDir.append( "/" );
    if( logFile.find( '/' ) == std::string::npos ) logFile = logBaseDir + logFile;

    // switches
    if( vm.count("daemonise") ) bDaemon = true;
    if( vm.count("rotate") ) bNoRotate = false;
    if( vm.count("nologconsole") ) bLogConsole = false;
    if( vm.count("logstderr") ) bLogStderr = true;
    if( vm.count("flushlogs") ) bFlushLogs = true;
    if( vm.count("main.nonucleus") ) bStartNucleus = false;
    if( vm.count("main.nosocket") ) bStartSocket = false;
    
    if( vm.count("help") ) 
    {
      std::cout << serverSwitches << serverOptions << "\n\n";
      optionsNucleus* d = new optionsNucleus();
      optionsNetworkIf* s = new optionsNetworkIf();
      d->parseOptions( ac, av );
      s->parseOptions( ac, av );
      delete d;
      delete s;

      return false;
    }

    if( vm.count("version") ) 
    {
      std::cout << APP_BASE_NAME << " - build no " << buildno << " build time " << buildtime << " mxDgram " << unixSocket::READ_BUF_SIZE << std::endl;
      return false;
    }
    
    if( vm.count("display_options") ) 
    {
      std::cout << displayOptions( );
      optionsNucleus* d = new optionsNucleus();
      optionsNetworkIf* s = new optionsNetworkIf();
      std::cout << "\nNucleus options\n" << "==================\n";
      d->parseOptions( ac, av );
      std::cout << "\nSocket options\n" << "==============\n";
      s->parseOptions( ac, av );
      delete d;
      delete s;
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
 * logs the parsed options
 * **/
void options::logOptions( )
{
  log.info( logger::LOWLEVEL ) << "\nMain options\n" << "============";
  log.info( logger::LOWLEVEL ) << displayOptions();
  optionsNucleus* d = new optionsNucleus();
  optionsNetworkIf* s = new optionsNetworkIf();
  log.info( logger::LOWLEVEL ) << "\nNucleus options\n" << "==================";
  d->parseOptions( argc, argv );
  log.info( logger::LOWLEVEL ) << d->displayOptions();
  log.info( logger::LOWLEVEL ) << "\nNetworkIf options\n" << "==============";
  s->parseOptions( argc, argv );
  log.info( logger::LOWLEVEL ) << s->displayOptions();
  delete d;
  delete s;
} // logOptions

/** 
 * displays the parsed options
 * **/
std::string options::displayOptions( )
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
