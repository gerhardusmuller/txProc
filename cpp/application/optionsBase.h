/**
 optionsBase - base system config class
 
 $Id: optionsBase.h 2591 2012-09-25 17:43:55Z gerhardus $
 Versioning: a.b.c a is a major release, b represents changes or new features, c represents bug fixes. 
 @version 1.0.0		18/01/2012		Gerhardus Muller		Script created
 @version 1.1.0		23/06/2012		Gerhardus Muller		Added main.bDropPermPriviledges

 @note

 @todo
 
 @bug

 Copyright Gerhardus Muller
 */

#if !defined( optionsBase_defined_ )
#define optionsBase_defined_

#include "logging/logger.h"
#include <boost/program_options.hpp>
namespace po = boost::program_options;

class optionsBase
{
  // Definitions
  public:
    static const int DEF_OUTPUT_WIDTH = 100;

    // Methods
  public:
    optionsBase( const char* objName, const char* theAppName );
    virtual ~optionsBase();
    virtual std::string toString ();
    po::variables_map& getVariableMap()                               { return vm; }
    virtual bool parseOptions( int ac, char* av[] );
    virtual void addAppOptions( po::options_description& appOptions ) {;}   ///< implement to additional application options
    virtual void logOptions();
    virtual std::string displayOptions();
    virtual std::string& getLogBaseDir()                              {return logBaseDir;}

  protected:

  private:

    // Properties
  public:

    // switches
    bool                        bLogConsole;              ///< true to log to the console
    bool                        bLogStderr;               ///< 1 to log on the console (stderr), 0 if not
    bool                        bFlushLogs;               ///< true to flush after each write

    // main
    std::string                 runAsUser;                ///< user to run as
    std::string                 statsUrl;                 ///< url for stats reporting
    std::string                 statsQueue;               ///< queue for stats event processing
    std::string                 configFile;               ///< config file to use
    std::string                 baseDir;                  ///< base directory for the application
    std::string                 logBaseDir;               ///< base directory for logging
    std::string                 abortEventQueue;          ///< queue to send abort events to
    std::string                 applicationEventQueue;    ///< queue to send application events to
    std::string                 applicationEventScript;   ///< standard application to invoke to handle application events
    std::string                 appBaseName;              ///< application base name
    std::string                 logFile;                  ///< full path to the log file
    std::string                 logGroup;                 ///< group for log file creation
    std::string                 txProcSocketPath;         ///< Unix domain socket path for txProc (parent)
    std::string                 txProcSocketLocalPath;    ///< Unix domain socket path for txProc (parent) - local side
    std::string                 buildNo;                  ///< executable build number
    std::string                 buildTime;                ///< executable build time

    int                         defaultLogLevel;          ///< defaultLogLevel
    int                         maintInterval;            ///< timer interval to use for background maintenance jobs and faxout checking
    int                         maxShutdownWaitTime;      ///< max time to wait for shutdown
    bool                        bDropPermPriviledges;     ///< normally the frameword drops the rights permanently but this does not allow the application to regain them - only has an effect if queueN.bRunPriviledged=1 is specified

  protected:
    logger&                     log;                      ///< class scope logger
    po::variables_map           vm;                       ///< variable map holding all config optionsBase

  private:
};	// class optionsBase

extern optionsBase* pOptions;

#endif // !defined( optionsBase_defined_)

