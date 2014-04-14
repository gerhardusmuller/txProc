/**
 options - system config class
 
 $Id: options.h 2483 2012-07-25 08:31:58Z gerhardus $
 Versioning: a.b.c a is a major release, b represents changes or new features, c represents bug fixes. 
 @version 1.0.0		10/11/2009		Gerhardus Muller		Script created
 @version 1.1.0		25/07/2012		Gerhardus Muller		added buildtime/buildno

 @note

 @todo
 
 @bug

	Copyright Notice
 */

#if !defined( options_defined_ )
#define options_defined_

#include "logging/logger.h"
#include <boost/program_options.hpp>
namespace po = boost::program_options;

class options
{
  // Definitions
  public:
    static const int DEF_OUTPUT_WIDTH = 100;
    static const char* APP_BASE_NAME;

    // Methods
  public:
    options( );
    virtual ~options();
    virtual std::string toString ();
    po::variables_map& getVariableMap( )  { return vm; }
    bool parseOptions( int ac, char* av[] );
    void logOptions( );
    bool bGetBDaemon( )                   { return bDaemon; }

  protected:

  private:
    std::string displayOptions( );

    // Properties
  public:
    int                         argc;                     ///< original argc;
    char**                      argv;                     ///< original argv
    std::string                 logFile;                  ///< full path to the log file
    bool                        bLogConsole;              ///< true to log to the console
    bool                        bLogStderr;               ///< 1 to log on the console (stderr), 0 if not
    bool                        bFlushLogs;               ///< true to flush after each write
    bool                        bNoRotate;                ///< do not auto rotate the recovery log on startup
    int                         defaultLogLevel;          ///< defaultLogLevel
    int                         logFilesToKeep;           ///< number of log files to keep with logrotate
    int                         statsInterval;            ///< stats interval in seconds
    int                         statsHourStart;           ///< hour in the day to start recording stats
    int                         statsHourStop;            ///< hour in the day to stop recording stats

    std::string                 statsUrl;               ///< url for stats reporting
    std::string                 statsChildrenAddress;   ///< comma separated list of children slaved to this server for stats purposes - can be either server names or IP addresses, leave blank to disable
    std::string                 statsChildrenService;   ///< children stats service - either a /etc/service entry or a port number
    std::string                 recoverFile;            ///< file to recover, do not start controller up
    std::string                 logrotatePath;          ///< path for the logrotate executable
    std::string                 logrotateScript;        ///< path for the logrotate script - normally in /etc/logrotate.d/
    std::string                 runAsUser;              ///< user to run as
    std::string                 logGroup;               ///< group for log file creation
    std::string                 baseDir;                ///< base directory for server
    std::string                 logBaseDir;             ///< base directory for server
    std::string                 configFile;             ///< config file to use
    std::string                 pidName;                ///< name of the pid file to use
    std::string                 defaultQueue;           ///< default queue for event processing
    std::string                 buildNo;                ///< executable build number
    std::string                 buildTime;              ///< executable build time
    bool                        bDaemon;                ///< run as a daemon
    bool                        bStartNucleus;          ///< true to start the nucleus process
    bool                        bStartSocket;           ///< true to start the dispatcher process

    // socket class requirements
    std::string                 packetSuccessReply;   ///< reply written on a socket as a result of processing (dispatching) successfully - set to empty to disable
    std::string                 packetFailReply;      ///< reply written on a socket as a result of failing to process (dispatch) successfully - typically not deserialised properly - set to empty to disable

  protected:
    logger&                     log;                    ///< class scope logger

  private:
    po::variables_map           vm;                 ///< variable map holding all config options
};	// class options

extern options* pOptions;

#endif // !defined( options_defined_)

