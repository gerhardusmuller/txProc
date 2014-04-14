/** @class scriptExec
 scriptExec - class to handle script exec requests
 
 $Id: scriptExec.cpp 2943 2013-11-19 09:30:01Z gerhardus $
 Versioning: a.b.c a is a major release, b represents changes or new features, c represents bug fixes. 
 @version 1.0.0		30/09/2009		Gerhardus Muller		Script created
 @version 1.1.0		21/07/2011		Gerhardus Muller		Added the childPid printout to the persistent app spawn function
 @version 1.2.0		21/08/2012		Gerhardus Muller		startup info command event for persistent apps
 @version 1.2.0		30/08/2012		Gerhardus Muller		made provision for a default script and queue management events
 @version 1.3.0		22/09/2012		Gerhardus Muller		changed process logging to make it easier to debug if the result object was deserialised; fixed a bug in success handling on an object in process
 @version 1.4.0		11/10/2012		Gerhardus Muller		finer grained reporting of the return status of a process for queue event management and the tracking of the pid of the process that has exited before it is started up again
 @version 1.5.0		28/02/2013		Gerhardus Muller		support for reading fragments for the output of a persistent process
 @version 1.6.0		06/11/2013		Gerhardus Muller		compilation under debian

 @note

 @todo
 
 @bug

	Copyright Notice
 */
#include <errno.h>
#include <string.h>
#include <sys/types.h> 
#include <sys/wait.h>
#include <vector>
#include "boost/regex.hpp"
#include <sys/resource.h>

#include "nucleus/scriptExec.h"
#include "nucleus/baseEvent.h"
#include "nucleus/queueManagementEvent.h"
#include "utils/utils.h"

/**
 Construction
 */
scriptExec::scriptExec( int thePid )
  : object( "scriptExec" )
{
  pid = thePid;
  childPid = -1;
  exitedChildPid = -1;
  termSignal = 0;
  exitStatus = 0;
  pipefdStdIn[0] = -1;
  pipefdStdIn[1] = -1;
  pipefdStdOut[0] = -1;
  pipefdStdOut[1] = -1;
  pipefdStdErr[0] = -1;
  pipefdStdErr[1] = -1;
  bPersistentApp = false;
  bParseResponseForObject = false;
  pRecSock = NULL;
}

scriptExec::scriptExec( int thePid, const std::string& perl, const std::string& shell, const std::string& execSuc, const std::string& execFail, const std::string& errorPref, const std::string& tracePref, const std::string& paramPref, const std::string& theQueueName, bool theParseResponseForObject, const std::string& theDefaultScript )
  : object( "scriptExec" ),
    perlPath(perl), 
    shellPath(shell), 
    execSuccess(execSuc), 
    execFailure(execFail), 
    errorPrefix(errorPref), 
    tracePrefix(tracePref), 
    paramPrefix(paramPref),
    ownQueue(theQueueName),
    defaultScript(theDefaultScript),
    bParseResponseForObject( theParseResponseForObject )
{
  char tmp[64];
  sprintf( tmp, "scriptExec-%s", theQueueName.c_str() );
  log.setInstanceName( tmp );
  pid = thePid;
  log.setAddPid( true );
  childPid = -1;
  exitedChildPid = -1;
  termSignal = 0;
  exitStatus = 0;
  pipefdStdIn[0] = -1;
  pipefdStdIn[1] = -1;
  pipefdStdOut[0] = -1;
  pipefdStdOut[1] = -1;
  pipefdStdErr[0] = -1;
  pipefdStdErr[1] = -1;
  bPersistentApp = false;
  pRecSock = NULL;
  pQueueManagement = NULL;
}	// scriptExec

/**
 Destruction
 */
scriptExec::~scriptExec()
{
  if( pRecSock != NULL ) delete pRecSock;
}	// ~scriptExec

/**
 Standard logging call - produces a generic text version of the scriptExec.
 Memory allocation / deleting is handled by this scriptExec.
 @return pointer to a string describing the state of the scriptExec.  The string has an unspecified lifetime and is not multi-thread safe
 */
std::string scriptExec::toString( )
{
  std::ostringstream oss;
  oss << typeid(*this).name() << ":" << this;
	return oss.str();
}	// toString

/**
 * parses standard results (result, error, trace timestamp)
 * if the bSuccess is false no further checking for success/fail is performed
 * if a success indication is found the result is set to success
 * if a fail indication is found the result is fail (overrides success)
 * if neither is found the result is fail
 * @param bSuccess inout parameter
 * @param result - result string that has been produced by script execution
 * **/
void scriptExec::parseStandardResponse( bool& bSuccess, const std::string& result )
{
  // only try and parse the result: if the script was successfully executed
  // i.e. did not fail all together or return a non-zero return code
  if( bSuccess )
  {
    bool bFoundSuccess = false;
    bool bFoundFail = false;
    bFoundSuccess = (result.find( execSuccess ) != std::string::npos );
    bFoundFail = (result.find( execFailure ) != std::string::npos );

    if( bFoundFail )
    {
      bSuccess = false;
      failureCause = "foundFail";
    }
    else if(!bFoundFail && !bFoundSuccess)
    {
      bSuccess = false;
      failureCause = "noFailOrSuccess";
    }
  } // if( bSuccess 

  // try and extract the error if any and the trace log timestamp if present
  if( !bSuccess )
  {
    std::string regStr = errorPrefix + "([^\\n]*)";
    boost::regex reg( regStr );
    boost::smatch m;
    std::string::const_iterator it = result.begin();
    std::string::const_iterator end = result.end();
    bool bRetval = boost::regex_search( it, end, m, reg );
    if( bRetval )
      errorString = m[1];
  } // if( !bSuccess
  { // for the tracePrefix we support multiple occurences - to be concatenated together
    std::string regStr = tracePrefix + "([^\\n]*)";
    boost::regex reg( regStr );
    boost::smatch m;
    std::string::const_iterator it = result.begin();
    std::string::const_iterator end = result.end();
    while( boost::regex_search( it, end, m, reg ) )
    {
      if( traceTimestamp.length() > 0 ) traceTimestamp += "-";
      traceTimestamp += m[1];
      it = m[0].second;
    } // while
  }
  {
    std::string regStr = paramPrefix + "([^\\n]*)";
    boost::regex reg( regStr );
    boost::smatch m;
    std::string::const_iterator it = result.begin();
    std::string::const_iterator end = result.end();
    bool bRetval = boost::regex_search( it, end, m, reg );
    if( bRetval )
      systemParam = m[1];
  }
} // parseStandardResponse

/**
 * process a script execution event
 * @param pEvent
 * @param result - result string that can optionally be returned
 * @param pResult - optional result object
 * @return true if success
 * **/
bool scriptExec::process( baseEvent* pEvent, std::string& result, baseEvent* &pResult ) 
{
  bool bSuccess;
  pResult = NULL;
  
  errorString.erase();
  traceTimestamp.erase();
  systemParam.erase();
  failureCause.erase();
  result.reserve( 4096 );
  
  try
  {
    bSuccess = execScript( pEvent, result );

    // try to deserialise the output of the script
    if( bSuccess && bParseResponseForObject && (result.length()>(baseEvent::FRAME_HEADER_LEN+baseEvent::BLOCK_HEADER_LEN)) )
      pResult = baseEvent::unSerialiseFromString( result );
    if( (pResult!=NULL) && (pResult->getType()==baseEvent::EV_RESULT) )
      bSuccess = pResult->isSuccess();

    if( (pResult==NULL) && pEvent->getStandardResponse() )
      parseStandardResponse( bSuccess, result );
  }
  catch( Exception e )
  {
    bSuccess = false;
    result.clear();
    result += "exception: ";
    result += e.getMessage( );
  }
  catch( std::runtime_error e )
  { // json-cpp throws runtime_error
    log.error( "process failed caught std::runtime_error:'%s' result:'%s'", e.what(),result.c_str() );
    bSuccess = false;
    result = "exception: ";
    result += e.what();
  } // catch
  catch( ... )
  {
    log.error( "process failed: caught unknown exception result:'%s'",result.c_str() );
    bSuccess = false;
    result = "exception: unknown";
  } // catch
  
  std::string queue = pEvent->getDestQueue();
  std::ostringstream oss;
  oss << "scriptExec success: " << bSuccess << " queue: '" << queue << "'";
  oss << " ref: " << pEvent->getRef( );
  if( errorString.length() > 0 ) { oss << " error: " << errorString; }
  if( traceTimestamp.length() > 0 ) { oss << " traceT: " << traceTimestamp; }
  if( systemParam.length() > 0 ) { oss << " param: " << systemParam; }
  if( failureCause.length() > 0 ) { oss << " failCause: " << failureCause; }
  if( pEvent->getTrace().length() > 0 ) { oss << " traceB|:" << pEvent->getTrace() << ":|traceE"; }
  if( pResult != NULL )
    oss << " pResult: " << pResult->toString();
  else
    oss << " result:\n" << result;
  if( !bSuccess ) { oss << " event: " << pEvent->toString(); }
  log.info( log.LOGMOSTLY ) << oss.str();
  return bSuccess;
} // process

/**
 * escapes the parameters
 * **/
std::string scriptExec::shellEscape( const std::string& str )
{
  std::string retString;
  boost::regex reg( "(')" );    // replace all single quotes with '\''
  retString = "'";              // single quote each parameter and escape single quotes
  retString += boost::regex_replace( str, reg, "'\\\\''" ); // the regex doubles the \ and the C-string
  retString += "'";
  return retString;
} // shellEscape

/**
 * builds the command line - escapes the parameters
 * **/
std::string scriptExec::buildCommandLine( const std::string& scriptCmd, baseEvent* pCommand )
{
  std::string cmdLine;
  cmdLine = scriptCmd;
  for( unsigned int i = 0; i < pCommand->scriptParamSize(); i++ )
  {
    cmdLine += " ";
    cmdLine += shellEscape( pCommand->getScriptParam(i) );
  }
  return cmdLine;
} // buildCommandLine

/**
 Executes the reporting script, captures stdout and stderr
 Only returns once the child exits
 @param pEvent
 @param resultLine - output returned
 @return true on success
 @exception on error
 */
bool scriptExec::execScript( baseEvent* pEvent, std::string& resultLine )
{
  bool bSuccess;
  try
  {
    spawnScript( pEvent, bPersistentApp );
    bSuccess = readPipe( resultLine );
  } // try
  catch( Exception e )
  {
    bSuccess = false;
  } // catch
  return bSuccess;
} // execScript

/**
 Executes the reporting script, captures stdout and stderr
 @param pEvent
 @param bPersistent
 @exception on error
 */
void scriptExec::spawnScript( baseEvent* pEvent, bool bPersistent )
{
  scriptCmd = pEvent->getScriptName();
  // if no script is defined and we have a default script use that
  if( (scriptCmd.empty() || (scriptCmd.length()==0)) && (defaultScript.length()>0) ) scriptCmd = defaultScript;
  
  bPersistentApp = bPersistent;
  std::string shell;   // contains the shell that executes the script
  std::string cmdLine;
  int index = 0;
  unsigned int numArgs = pEvent->scriptParamSize();
  typedef const char* cp;
  cp* argsArr = new cp[numArgs+5];

  // make sure the previous child has exited
  waitForChildExit( );
  
  switch( pEvent->getType() )
  {
    case baseEvent::EV_SCRIPT:
      argsArr[index++] = "sh";
      argsArr[index++] = "-c";
      cmdLine = buildCommandLine( scriptCmd, pEvent );
      argsArr[index++] = cmdLine.c_str(); 
      shell = shellPath;
      break;
    case baseEvent::EV_PERL:
      argsArr[index++] = "perl";
      argsArr[index++] = scriptCmd.c_str();
      for( unsigned int i = 0; i < numArgs; i++ )
        argsArr[index++] = pEvent->getScriptParamAsCStr( i );
      shell = perlPath;
      break;
    case baseEvent::EV_BIN:
      argsArr[index++] = scriptCmd.c_str();
      for( unsigned int i = 0; i < numArgs; i++ )
        argsArr[index++] = pEvent->getScriptParamAsCStr( i );
      shell = scriptCmd;
      break;
    default:
      throw Exception( log, log.ERROR, "spawnScript: unknown type %s", pEvent->typeToString() );
  } // switch

  // terminate the array
  argsArr[index] = (const char*)NULL;

  // log the command line
  std::ostringstream oss;
  oss << shell << " ";
  int i = 0;
  while( argsArr[i] != NULL )
    oss << "\"" << argsArr[i++] << "\" ";
  log.info( log.LOGMOSTLY ) << "spawnScript:" << (bPersistentApp?" (persistent)":"") << " cmd:"<< oss.str();
  
  // we always use a pipe to collect the stdout from the child. for persistent apps
  // we create two more pipes - one for stdin and one for stderr. for non-persistent
  // apps we send the child's stderr to the stdout
  int res = pipe( pipefdStdOut );
  if( res != 0 )
  {
    delete[] argsArr;
    throw Exception( log, log.ERROR, "spawnScript: pipe out create failed %s", strerror( errno ) );
  } // if
  if( bPersistentApp )
  {
    res = pipe( pipefdStdIn );
    if( res != 0 )
    {
      delete[] argsArr;
      throw Exception( log, log.ERROR, "spawnScript: pipe in create failed %s", strerror( errno ) );
    } // if
    res = pipe( pipefdStdErr );
    if( res != 0 )
    {
      delete[] argsArr;
      throw Exception( log, log.ERROR, "spawnScript: pipe err create failed %s", strerror( errno ) );
    } // if
  } // if
  
  exitedChildPid = -1;
  childPid = fork();
  switch( childPid ) 
  {
    case -1:  // error
      delete[] argsArr;
      throw Exception( log, log.ERROR, "spawnScript: could not fork" );
      break;
    case 0: // child
      {
        // always capture stdout to the pipe pipefdStdOut. if not persistent
        // then capture stderr to the same pipe
        // if persistent capture stderr and stdin to their respective pipes
        int retDup;
        retDup = dup2( pipefdStdOut[1], STDOUT_FILENO );
        if( retDup == -1 ) log.error( "spawnScript: dup2 pipefdStdOut[1] STDOUT_FILENO failed %s", strerror( errno ) );
        if( bPersistentApp )
        {
          retDup = dup2( pipefdStdErr[1], STDERR_FILENO );
          if( retDup == -1 ) log.error( "spawnScript: dup2 pipefdStdOut[1] STDOUT_FILENO failed %s", strerror( errno ) );
          retDup = dup2( pipefdStdIn[0], STDIN_FILENO );
          if( retDup == -1 ) log.error( "spawnScript: dup2 pipefdStdIn[0] STDIN_FILENO failed %s", strerror( errno ) );
        } // else
        else
        {
          retDup = dup2( pipefdStdOut[1], STDERR_FILENO );
          if( retDup == -1 ) log.error( "spawnScript: dup2 pipefdStdOut[1] STDERR_FILENOfailed %s", strerror( errno ) );
        } // if
        pclose( pipefdStdIn[0] );
        pclose( pipefdStdIn[1] );
        pclose( pipefdStdOut[0] );
        pclose( pipefdStdOut[1] );
        pclose( pipefdStdErr[0] );
        pclose( pipefdStdErr[1] );

        // spawn the shell or command
        res = execv( shell.c_str(), (char* const*)argsArr );
        if( res == -1 )
          log.error( "spawnScript: child error: %s", strerror( errno ) );
        sleep( 1 );
        exit( 1 );
      }
      break;
    default:  // parent
        pclose( pipefdStdOut[1] ); // parent reads from this side - [1] is the write side
        pclose( pipefdStdErr[1] ); // parent reads from this side - [1] is the write side
        pclose( pipefdStdIn[0] );  // parent writes to this pipe - [0] is the read side
        if( bPersistentApp )
        {
          if( pQueueManagement != NULL ) pQueueManagement->genEvent( queueManagementEvent::QMAN_PSTARTUP );
          if( pRecSock != NULL ) delete pRecSock;
          pRecSock = new unixSocket( pipefdStdOut[0], unixSocket::ET_WORKER_PIPE, false, "pipefdStdOut" );
          //          pRecSock->setNonblocking();
          pRecSock->setPipe();
          pRecSock->initPoll( 2 );
          pRecSock->resetPoll();
          pRecSock->addReadFd( pipefdStdOut[0] );
          pRecSock->addReadFd( pipefdStdErr[0] );
          pRecSock->setPollTimeout( 2000 );
          log.info( log.LOGNORMAL, "spawnScript: pipefdStdIn:%d-%d pipefdStdOut:%d-%d pipefdStdErr:%d-%d childPid:%d", pipefdStdIn[0], pipefdStdIn[1], pipefdStdOut[0], pipefdStdOut[1], pipefdStdErr[0], pipefdStdErr[1], childPid );

          // generate a startup info command event to the persistent app
          baseEvent cmd( baseEvent::EV_COMMAND );
          cmd.setCommand( baseEvent::CMD_PERSISTENT_APP );
          cmd.addParam( "cmd", "startupinfo" );
          cmd.addParam( "ownqueue", ownQueue );
          cmd.addParam( "workerpid", pid );
          //cmd.serialise( pipefdStdIn[1], baseEvent::FD_PIPE );
          readWritePipe( &cmd );
        } // if
      break;
  } // switch childPid
  delete[] argsArr;
} // spawnScript

/**
 * writes a dispatchEvent derived object to the pipe and expects to read one 
 * back again - only used for persistent apps
 * @param pReq - request object
 * @exception if not in persistent mode or unable to unserialise response
 * */
baseEvent* scriptExec::readWritePipe( baseEvent* pReq )
{
  if( !bPersistentApp || (pRecSock==NULL) ) throw Exception( log, log.ERROR, "readWritePipe: not in persistent mode" );
  baseEvent* pEvent = NULL;

  // write the request to the child process's stdIn pipe
  if( pReq != NULL )
  {
    int ret = pReq->serialise( pipefdStdIn[1], baseEvent::FD_PIPE );
    log.debug( log.LOGONOCCASION, "readWritePipe: wrote %d bytes to '%s'", ret, scriptCmd.c_str() );
  } // if

  // read a single response object from the app's stdout
  bWaitingForResponse = true;
  int fd;
  while( bWaitingForResponse )
  {
    bool bReady = false;
    try
    {
      // if stdin/stdout is closed abort - the child has exited
      int lastErrorFd = pRecSock->getLastErrorFd();
      if( lastErrorFd > -1 )
        throw Exception( log, log.INFO, "readWritePipe: child stdin/out was closed" );
      bReady = pRecSock->multiFdWaitForEvent( );
    }
    catch( Exception e )
    {
      throw;
    } // catch
    if( bReady )
    {
      while( ( fd = pRecSock->getNextFd() ) > 0 )
      {
        log.debug( log.LOGNORMAL, "readWritePipe: fd %d has data", fd );
        if( fd == pipefdStdOut[0] )
        {
          pEvent = baseEvent::unSerialise( pRecSock );
          if( pEvent != NULL ) bWaitingForResponse = false;
          //if( pEvent == NULL ) throw Exception( log, log.ERROR, "readWritePipe: pEvent == NULL" );
        } // if
        else if( fd == pipefdStdErr[0] )
        {
          char line[16384];
          int len = read( fd, line, 16383 );
          line[len] = '\0';
          log.warn( log.LOGMOSTLY ) << "readWritePipe: stderr output: " << line;
        } // else if
        else
        {
          log.error( "readWritePipe: fd %d has data yet I do not know it", fd );
        } // else
      } // while
    } // if
    else
      log.debug( log.LOGNORMAL, "readWritePipe: multiFdWaitForEvent returned false" );
  } // while bWaitingForResponse
  if( pEvent == NULL ) log.warn( log.LOGMOSTLY, "readWritePipe: returning a NULL object" );
  return pEvent;
} // readWritePipe

/**
 * reads the stdio/stderr pipes until it closes
 * closes the write handle if it is not closed
 * waits for the child process to exit
 * @param result - stdout/stderr output from child
 * @return true if child exited with 0
 * */
bool scriptExec::readPipe( std::string& result )
{
  FILE* fd = fdopen( pipefdStdOut[0], "r" );
  char line[4096];
  while( fgets( line, sizeof (line)-1, fd ) )
  {
    line[4095] = '\0';
    result.append( line );
  } // while

  fclose( fd );
  return waitForChildExit();
} // readPipe

/**
 * closes the pipe handle if not already closed
 * @param fd
 * **/
void scriptExec::pclose( int& fd )
{
  if( fd != -1 )
  {
    close( fd );
    fd = -1;
  } // if
} // scriptExec

/**
 * waits for the spawned child to exit
 * @return true for return value of 0
 * **/
bool scriptExec::waitForChildExit( )
{
  int waitStatus;
  bool bSuccess = true;

  if( childPid == -1 ) return true;

  // close all open pipe handles
  pclose( pipefdStdIn[0] );
  pclose( pipefdStdIn[1] );
  pclose( pipefdStdOut[0] );
  pclose( pipefdStdOut[1] );
  pclose( pipefdStdErr[0] );
  pclose( pipefdStdErr[1] );

  // wait for the child to exit
  int res = waitpid( childPid, &waitStatus, 0 );
  exitedChildPid = childPid;
  exitStatus = WEXITSTATUS( waitStatus );
  if( (res == -1) || (waitStatus != 0) )
  {
    log.error( ) << "waitForChildExit: bad exit status:'" << utils::printExitStatus(waitStatus) << "' for '" << scriptCmd << "' err: " << ((res==-1)?strerror(errno):"none");
    failureCause = "execFailure";
    bSuccess = false;
    if( WIFSIGNALED( waitStatus ) )
      termSignal = WTERMSIG( waitStatus );
    else
      termSignal = 0;
  } // if
  childPid = -1;
  return bSuccess;
} // waitForChildExit

/**
 * sets resource limits
 * the hard limits are typically set by ulimit in the shell
 * @param resource
 * @param newLimit
 * **/
void scriptExec::setResourceLimit( int resource, unsigned long newLimit )
{
  const char* limitName = resourceLimitToStr( resource );
  struct rlimit curLimit;
  int retVal = getrlimit( resource, &curLimit );
  if( retVal == -1 ) throw new Exception( log, log.WARN, "setResourceLimit: getrlimit error for resource:%s(%d) - %s", limitName, resource, strerror(errno) );
  log.info( log.LOGMOSTLY, "setResourceLimit: resource:%s currently:%lu max:%lu new value:%lu", limitName, curLimit.rlim_cur, curLimit.rlim_max, newLimit );
  curLimit.rlim_cur = newLimit;
  retVal = setrlimit( resource, &curLimit );
  if( retVal == -1 ) throw new Exception( log, log.WARN, "setResourceLimit: setrlimit error for resource:%s(%d) - %s", limitName, resource, strerror(errno) );
} // setResourceLimit
void scriptExec::setResourceLimit( const std::string& resource, unsigned long newLimit )
{
  int resourceNum = 0;
  if( resource.compare("RLIMIT_AS") == 0 )
    resourceNum = RLIMIT_AS;
  else if( resource.compare("RLIMIT_CPU") == 0 )
    resourceNum = RLIMIT_CPU;
  else if( resource.compare("RLIMIT_DATA") == 0 )
    resourceNum = RLIMIT_DATA;
  else if( resource.compare("RLIMIT_FSIZE") == 0 )
    resourceNum = RLIMIT_FSIZE;
  else if( resource.compare("RLIMIT_STACK") == 0 )
    resourceNum = RLIMIT_STACK;
  else 
    throw new Exception( log, log.WARN, "setResourceLimit: did not recognise resource:'%s'", resource.c_str() );

  return setResourceLimit( resourceNum, newLimit );
} // setResourceLimit

/**
 * as a side effect confirm that we support this particular resource limit
 * @exception on resource not supported
 * **/
const char* scriptExec::resourceLimitToStr( int resource )
{
  switch( resource )
  {
    case RLIMIT_AS:
      return "RLIMIT_AS";
      break;
    case RLIMIT_CPU:
      return "RLIMIT_CPU";
      break;
    case RLIMIT_DATA:
      return "RLIMIT_DATA";
      break;
    case RLIMIT_FSIZE:
      return "RLIMIT_FSIZE";
      break;
    case RLIMIT_STACK:
      return "RLIMIT_STACK";
      break;
    default:
      throw new Exception( log, log.ERROR, "resourceLimitToStr: resource:%d not supported", resource );
  } // switch
} // resourceLimitToStr
