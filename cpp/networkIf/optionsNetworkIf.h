/**
 optionsNetworkIf - system config class for the dispatcher
 the main options class is supposed to derive from this class
 
 $Id: optionsNetworkIf.h 2931 2013-10-16 09:47:29Z gerhardus $
 Versioning: a.b.c a is a major release, b represents changes or new features, c represents bug fixes. 
 @version 1.0.0		16/09/2008		Gerhardus Muller		Script created
 @version 1.1.0		16/10/2013		Gerhardus Muller		added tcpListenAddr

 @note

 @todo
 
 @bug

	Copyright Notice
 */

#if !defined( optionsNetworkIf_defined_ )
#define optionsNetworkIf_defined_

#include "utils/object.h"
#include <boost/program_options.hpp>
namespace po = boost::program_options;

class optionsNetworkIf
{
  // Definitions
  public:
    static const int DEF_OUTPUT_WIDTH = 100;

    // Methods
  public:
    optionsNetworkIf( );
    virtual ~optionsNetworkIf();
    virtual std::string toString ();
    po::variables_map& getVariableMap( )  { return vm; }
    bool parseOptions( int ac, char* av[] );
    void logOptions( );
    std::string displayOptions( );
    bool existVar( const char* name )     {const po::variable_value& val = vm[name];return !val.empty();};
    void getAsString( const char* name, std::string& value );
    bool isString( const char* name );
    int getAsInt( const char* name, int defaultVal=0 );
    bool isInt( const char* name );

  protected:

  private:

    // Properties
  public:
    logger&                     log;                  ///< class scope logger

    // switches
    bool                        bLogConsole;          ///< 1 to log on the console (stdout), 0 if not
    bool                        bLogStderr;           ///< 1 to log on the console (stderr), 0 if not
    bool                        bFlushLogs;               ///< true to flush after each write

    // mserver
    std::string                 runAsUser;            ///< user to run as
    std::string                 logGroup;             ///< group for log file creation
    std::string                 statsUrl;             ///< url for stats reporting
    std::string                 configFile;           ///< config file to use

    // socket
    std::string                 logFile;              ///< log file to use
    int                         defaultLogLevel;      ///< defaultLogLevel
    int                         maxTcpConnections;    ///< max number of simultaneous TCP connections open
    std::string                 socketService;        ///< service to listen on in /etc/services
    std::string                 unixSocketPath;       ///< unix socket path to submit events to the dispatcher from outside mserver  
    std::string                 socketGroup;          ///< group for Unix socket ownership
    std::string                 listenAddr;           ///< tcp Listen address

  protected:

  private:
    po::variables_map           vm;                   ///< variable map holding all config options
};	// class optionsNetworkIf

extern optionsNetworkIf* pOptionsNetworkIf;
  
#endif // !defined( optionsNetworkIf_defined_)

