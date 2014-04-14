/**
 scriptExec - class to handle script exec requests
 
 $Id: scriptExec.h 2622 2012-10-11 15:24:56Z gerhardus $
 Versioning: a.b.c a is a major release, b represents changes or new features, c represents bug fixes. 
 @version 1.0.0		30/09/2009		Gerhardus Muller		Script created
 @version 1.2.0		21/08/2012		Gerhardus Muller		startup info command event for persistent apps
 @version 1.3.0		11/10/2012		Gerhardus Muller		finer grained reporting of the return status of a process for queue event management

 @note

 @todo
 
 @bug

	Copyright Notice
 */

#if !defined( scriptExec_defined_ )
#define scriptExec_defined_

#include "utils/object.h"
#include "utils/unixSocket.h"

class baseEvent;
class queueManagementEvent;

//class scriptExec : public event - geen idee hoekom nie
class scriptExec : public object
{
  // Definitions
  public:

    // Methods
  public:
    scriptExec( int thePid );
    scriptExec( int thePid,const std::string& perl,const std::string& shell,const std::string& execSuccess,const std::string& execFailure,const std::string& errorPrefix,const std::string& tracePrefix,const std::string& paramPrefix,const std::string& theQueueName,bool theParseResponseForObject,const std::string& theDefaultScript );
    virtual ~scriptExec();
    virtual std::string toString ();
    bool process( baseEvent* pEvent, std::string& result, baseEvent* &pResult ); 
    int getChildPid( )                                              {return childPid;}
    int getExitedChildPid( )                                        {return exitedChildPid;}
    int getWorkerPid( )                                             {return pid;}
    int getTermSignal( )                                            {return termSignal;}      ///< signal that killed the process otherwise 0
    int getExitStatus( )                                            {return exitStatus;}      ///< return value of the process
    void setErrorString( const char* s )                            {errorString=s;}
    void setFailureCause( const char* s )                           {failureCause=s;}
    std::string getErrorString( )                                   {return errorString;}
    std::string getTraceTimestamp( )                                {return traceTimestamp;}
    std::string getFailureCause( )                                  {return failureCause;}
    std::string getSystemParam( )                                   {return systemParam;}
    std::string getScriptCmd( )                                     {return scriptCmd;}
    void spawnScript( baseEvent* pEvent, bool bPersistent );
    baseEvent* readWritePipe( baseEvent* pReq );
    bool waitForChildExit( );
    void setResourceLimit( int resource, unsigned long newLimit );
    void setResourceLimit( const std::string& resource, unsigned long newLimit );
    void setManagementObj( queueManagementEvent* theObj )           {pQueueManagement=theObj;}
  
  private:
    bool execScript( baseEvent* pEvent, std::string& resultLine );
    void parseStandardResponse( bool& bSuccess, const std::string& result );
    std::string buildCommandLine( const std::string& theScriptCmd, baseEvent* pEvent );
    std::string shellEscape( const std::string& str );
    bool readPipe( std::string& result );
    void pclose( int& fd );
    const char* resourceLimitToStr( int resource );

    // Properties
  public:

  protected:

  private:
    const std::string               perlPath;           ///< path to the perl executable
    const std::string               shellPath;          ///< path to the shell
    const std::string               execSuccess;        ///< indication of success for scripts
    const std::string               execFailure;        ///< indication of failure for scripts
    const std::string               errorPrefix;        ///< prefix for an error string in the result
    const std::string               tracePrefix;        ///< prefix for a trace timestamp string in the result
    const std::string               paramPrefix;        ///< prefix for a system parameter in the result
    const std::string               ownQueue;           ///< queue on which this worker lives
    std::string                     errorString;        ///< error string returned by executed script if any
    std::string                     traceTimestamp;     ///< trace timestamp returned by script if any
    std::string                     failureCause;       ///< where in the process it was failed - mainly intended for system debug
    std::string                     systemParam;        ///< system parameter in the result - typically the activity_log_id
    std::string                     scriptCmd;          ///< current script executing
    std::string                     defaultScript;      ///< if no script is specified for EV_PERL/EV_SCRIPT/EV_BIN events
    pid_t                           childPid;           ///< child pid
    pid_t                           exitedChildPid;     ///< pid of the child that has just exited - we need this value for a queue management event
    bool                            bPersistentApp;     ///< true if running a persistent app
    bool                            bWaitingForResponse;///< true while readWritePipe is waiting
    int                             pid;                ///< process pid - same as workerPid or wpid
    int                             termSignal;         ///< signal number that terminated the process; 0 if not
    int                             exitStatus;         ///< exit status of the process - least significant 8 bits of status
    int                             pipefdStdIn[2];     ///< child stdin
    int                             pipefdStdOut[2];    ///< child stdout
    int                             pipefdStdErr[2];    ///< child stderr
    unixSocket*                     pRecSock;           ///< socket for accepting incoming events
    queueManagementEvent*           pQueueManagement;   ///< class that generates queue management events
    bool                            bParseResponseForObject;  ///< true to try and parse the execution output for an object
};	// class scriptExec

#endif // !defined( scriptExec_defined_)

