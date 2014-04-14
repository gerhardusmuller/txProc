/** @class optionsNetworkIf
 optionsNetworkIf - system config class for the dispatcher
 
 $Id: optionsNetworkIf.cpp 2931 2013-10-16 09:47:29Z gerhardus $
 Versioning: a.b.c a is a major release, b represents changes or new features, c represents bug fixes. 
 @version 1.0.0		16/09/2008		Gerhardus Muller		Script created
 @version 1.1.0		16/10/2013		Gerhardus Muller		added tcpListenAddr

 @note

 @todo
 
 @bug

	Copyright Notice
 */

#include "networkIf/optionsNetworkIf.h"
#include "options.h"
#include <iostream>
#include <fstream>

/**
 Construction
 */
optionsNetworkIf::optionsNetworkIf( )
  : log( logger::getInstance( "optionsNetworkIf" ) )
{
}	// optionsNetworkIf

/**
 Destruction
 */
optionsNetworkIf::~optionsNetworkIf()
{
  delete &log;
}	// ~optionsNetworkIf

/**
 Standard logging call - produces a generic text version of the options.
 Memory allocation / deleting is handled by this options.
 @return pointer to a string describing the state of the options.  The string has an unspecified lifetime and is not multi-thread safe
 */
std::string optionsNetworkIf::toString( )
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
bool optionsNetworkIf::parseOptions( int ac, char* av[] )
{
  try 
  {
    bLogConsole = true;
    bLogStderr = false;
    bFlushLogs = false;
    configFile = pOptions->configFile;
    logFile = pOptions->logBaseDir;
    logFile.append( "networkIf.log" );
    unixSocketPath = pOptions->logBaseDir;
    unixSocketPath.append( pOptions->APP_BASE_NAME );
    unixSocketPath.append( ".sock" );

    // options that control - typically global options
    // precede long options by a '--'
    po::options_description serverSwitches("switches");
    serverSwitches.add_options()
      ("help,h", "produce help message")    
      ("display_options,d", "display parsed options")
      ("nologconsole,L", "do not log to stdout")
      ("logstderr,s", "log to stderr")
      ;

    po::options_description serverOptions("[main] options", DEF_OUTPUT_WIDTH);
    serverOptions.add_options()
      ("main.configFile,c", po::value<std::string>(&configFile)->default_value(pOptions->configFile), "config file")
      ("main.runAsUser", po::value<std::string>(&runAsUser)->default_value("uucp"), "run as user")
      ("main.statsUrl", po::value<std::string>(&statsUrl), "Url for reporting stats")
      ;

    // socket options
    po::options_description socketOptions("[socket] options", DEF_OUTPUT_WIDTH);
    socketOptions.add_options()
      ("networkIf.logFile", po::value<std::string>(&logFile)->default_value(logFile), "log file - created in main.logBaseDir by default")
      ("networkIf.defaultLogLevel", po::value<int>(&defaultLogLevel)->default_value(5), "log levels 1-10 - only levels less or equal to this will be logged")
      ("networkIf.socketService", po::value<std::string>(&socketService)->default_value( "txproc" ), "service to listen on in /etc/services")
      ("networkIf.maxTcpConnections", po::value<int>(&maxTcpConnections)->default_value(100), "max number of simultaneous TCP connections open")
      ("networkIf.unixSocketPath", po::value<std::string>(&unixSocketPath)->default_value(unixSocketPath), "unix socket path to submit events to the dispatcher from outside mserver - created in main.logBaseDir by default")
      ("networkIf.socketGroup", po::value<std::string>(&socketGroup)->default_value("uucp"), "group unix domain socket owner")
      ("networkIf.listenAddr", po::value<std::string>(&listenAddr)->default_value("INADDR_ANY"), "INADDR_ANY,hostname,hostIp or empty in which case hostname will be used")
     ;
    
    po::options_description cmdline_options;
    cmdline_options.add( serverSwitches ).add( serverOptions ).add( socketOptions );

    po::options_description config_file_options;
    config_file_options.add( serverSwitches ).add( serverOptions ).add( socketOptions );
    
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
  
    // build the full log file name if required
    if( logFile.find( '/' ) == std::string::npos ) logFile = pOptions->logBaseDir + logFile;
    // build the full unix socket name if required
    if( unixSocketPath.find( '/' ) == std::string::npos ) unixSocketPath = pOptions->logBaseDir + unixSocketPath;

    // switches
    if( vm.count("nologconsole") ) bLogConsole = false;
    if( vm.count("logstderr") ) bLogStderr = true;
    if( vm.count("flushlogs") ) bFlushLogs = true;

    if( vm.count("help") ) 
    {
      std::cout << "\nNetworkIf options\n" << "==============\n";
      std::cout << serverSwitches << serverOptions << socketOptions << "\n\n";
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
  return true;
} // parseOptions

/**
 * returns a named option (typically has no description)
 * please confirm that the variable exists existVar() and isString()
 * @return the string if the value exists and is of the correct type
 * **/
void optionsNetworkIf::getAsString( const char* name, std::string& value )
{
  const po::variable_value& val = vm[name];
  if( !val.empty() )
  {
    const boost::any& p = val.value();
    if( p.type() == typeid(std::string) )
      value = boost::any_cast<std::string>(p);
  } // if
} // getAsString
bool optionsNetworkIf::isString( const char* name )
{
  bool b = false;
  const po::variable_value& val = vm[name];
  if( !val.empty() )
  {
    const boost::any& p = val.value();
    if( p.type() == typeid(std::string) ) b = true;
  } // if
  return b;
} // isString
/**
 * returns a named option (typically has no description)
 * @return the integer value or 0 if the value does not exist
 * or is not the correct type
 * **/
int optionsNetworkIf::getAsInt( const char* name, int value )
{
  const po::variable_value& val = vm[name];
  if( !val.empty() )
  {
    const boost::any& p = val.value();
    if( p.type()==typeid(int) )
      value = boost::any_cast<int>(p);
  } // if
  return value;
} // getAsInt
bool optionsNetworkIf::isInt( const char* name )
{
  bool b = false;
  const po::variable_value& val = vm[name];
  if( !val.empty() )
  {
    const boost::any& p = val.value();
    if( p.type() == typeid(int) ) b = true;
  } // if
  return b;
} // isInt

/**
 * logs the parsed options
 * **/
void optionsNetworkIf::logOptions( )
{
  log.info( log.LOGNORMAL ) << "Parsed options: " << displayOptions();
} // logOptions

/** 
 * displays the parsed options
 * **/
std::string optionsNetworkIf::displayOptions( )
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
