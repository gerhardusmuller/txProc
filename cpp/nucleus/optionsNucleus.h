/**
 optionsNucleus - system config class for nucleus
 
 $Id: optionsNucleus.h 2555 2012-09-04 14:12:00Z gerhardus $
 Versioning: a.b.c a is a major release, b represents changes or new features, c represents bug fixes. 
 @version 1.0.0		22/09/2009		Gerhardus Muller		Script created

 @note

 @todo
 
 @bug

	Copyright Notice
 */

#if !defined( optionsNucleus_defined_ )
#define optionsNucleus_defined_

#include "utils/object.h"
#include <map>
#include <boost/program_options.hpp>
namespace po = boost::program_options;

typedef std::map<std::string,std::string> optionsMapT;
typedef optionsMapT::iterator optionsMapIteratorT;
typedef std::pair<std::string,std::string> optionsMapPairT;

class optionsNucleus
{
  // Definitions
  public:
    static const int DEF_OUTPUT_WIDTH = 100;

    // Methods
  public:
    optionsNucleus( );
    virtual ~optionsNucleus();
    virtual std::string toString ();
    po::variables_map& getVariableMap( )  { return vm; }
    bool parseOptions( int ac, char* av[] );
    void logOptions( );
    std::string displayOptions( );
    bool existVar( const char* name );
    bool getAsString( const char* name, std::string& value );
    int getAsInt( const char* name, int defaultVal=0 );

  protected:

  private:

    // Properties
  public:
    logger&                     log;                  ///< class scope logger

    // switches
    bool                        bLogConsole;          ///< 1 to log on the console (stdout), 0 if not
    bool                        bLogStderr;           ///< 1 to log on the console (stderr), 0 if not
    bool                        bFlushLogs;           ///< true to flush after each write

    // generic
    std::string                 runAsUser;            ///< user to run as
    std::string                 logGroup;             ///< group for log file creation
    std::string                 statsUrl;             ///< url for stats reporting
    std::string                 statsQueue;           ///< queue for stats event processing
    std::string                 configFile;           ///< config file to use

    // nucleus
    std::string                 logFile;              ///< log file to use
    std::string                 statsDir;             ///< stats directory to use
    std::string                 activeQueues;         ///< comma separated list of active queue names
    std::string                 notLocalqueueRouterQueue; ///< queue that handles events destined for remote nodes
    std::string                 unixSocketPath;       ///< unix socket path to submit events to the dispatcher from outside
    std::string                 unixSocketStreamPath; ///< unix socket path to submit events to the dispatcher from outside - stream interface
    std::string                 socketGroup;          ///< group for Unix socket ownership
    int                         defaultLogLevel;      ///< defaultLogLevel
    unsigned int                maxNetworkDescriptors;///< indication of the maximum num of descriptors in the epoll object
    unsigned int                maintInterval;        ///< timer interval used for maintenance, this includes event expiration, max exec times and the delay queue
    unsigned int                expiredEventInterval; ///< interval in seconds between checks for expired events in the queue
    unsigned int                maxNumQueues;         ///< max number of queues to provision for
    unsigned int                bLogQueueStatus;      ///< logs queue status on maintenance interval

    // worker
    std::string                 perlPath;             ///< path to the perl executable
    std::string                 shellPath;            ///< path to the shell
    std::string                 execSuccess;          ///< indication of success for scripts
    std::string                 execFailure;          ///< indication of failure for scripts
    std::string                 urlSuccess;           ///< indication of success for urls
    std::string                 urlFailure;           ///< indication of failure for urls
    std::string                 errorPrefix;          ///< prefix for an error string in the result
    std::string                 tracePrefix;          ///< prefix for a trace timestamp string in the result
    std::string                 paramPrefix;          ///< prefix for a system parameter string in the result
    int                         workerDebugLevel;     ///< debug levels 1-10 - only levels less than this will be logged
    unsigned int                maxGetRequestLength;  ///< requests longer than this use POSTs
    unsigned int                persistentAppRespawnDelay;  ///< delay after a persistent app has exited before it is respawned
    unsigned int                rlimitAs;             ///< the maximum size of the process's virtual memory (address space) in bytes
    unsigned int                rlimitCpu;            ///< CPU time limit in seconds. When the process reaches the soft limit, it is sent a SIGXCPU signal
    unsigned int                rlimitData;           ///< the maximum size of the process's data segment (initialized data, uninitialized data, and heap)
    unsigned int                rlimitFsize;          ///< the maximum size of files that the process may create
    unsigned int                rlimitStack;          ///< the maximum size of the process stack, in bytes

  protected:

  private:
    po::variables_map           vm;                   ///< variable map holding all config options
    std::map<std::string,std::string> queueOptionMap; ///< map containing all the queues
};	// class optionsNucleus

extern optionsNucleus* pOptionsNucleus;
  
#endif // !defined( optionsNucleus_defined_)

