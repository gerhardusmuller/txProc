/**
 urlRequest - class to handle url requests
 
 $Id: urlRequest.h 2549 2012-09-03 19:20:15Z gerhardus $
 Versioning: a.b.c a is a major release, b represents changes or new features, c represents bug fixes. 
 @version 1.0.0		30/09/2009		Gerhardus Muller		Script created
 @version 1.1.0		21/08/2012		Gerhardus Muller		added setMaxTimeToRun

 @note

 @todo
 
 @bug

	Copyright Notice
 */

#if !defined( urlRequest_defined_ )
#define urlRequest_defined_

#define HAVE_CONFIG_H
#include <curlpp/cURLpp.hpp>
#include <curlpp/Easy.hpp>
#undef HAVE_CONFIG_H

#include "utils/object.h"

class baseEvent;
class queueManagementEvent;

class urlRequest : public object
{
  // Definitions
  public:

    // Methods
  public:
    urlRequest( int thePid,const std::string& urlSuccess,const std::string& urlFailure,const std::string& errorPrefix,const std::string& tracePrefix,const std::string& paramPrefix,int theTimeout,const std::string& theQueue,bool theParseResponseForObject,const std::string& theDefaultUrl );
    virtual ~urlRequest();
    virtual std::string toString ();
    bool process( baseEvent* pEvent, std::string& result, baseEvent* &pResult ); 
    size_t writeMemoryCallback(char* ptr, size_t size, size_t nmemb);
    void setErrorString( const char* s )                            {errorString=s;}
    void setFailureCause( const char* s )                           {failureCause=s;}
    void setMaxTimeToRun( int max )                                 {timeout=max;}
    std::string getErrorString( )                                   {return errorString;}
    std::string getTraceTimestamp( )                                {return traceTimestamp;}
    std::string getFailureCause( )                                  {return failureCause;}
    std::string getSystemParam( )                                   {return systemParam;}
    void setManagementObj( queueManagementEvent* theObj )           {pQueueManagement=theObj;}

  private:
    void parseStandardResponse( bool& bSuccess, const std::string& result );
    bool bNeedTranslation( const char c );
    void urlEncode( std::string& encoded, const char* unencoded );

    // Properties
  public:

  protected:

  private:
    std::string                     url;                ///< the url to call
    std::string                     urlResult;          ///< output of the http call
    std::string                     builtUrl;           ///< constructed URL
    long                            responseCode;       ///< http response code
    const std::string               urlSuccess;         ///< indication of success for urls
    const std::string               urlFailure;         ///< indication of failure for urls
    const std::string               errorPrefix;        ///< prefix for an error string in the result
    const std::string               tracePrefix;        ///< prefix for a trace timestamp string in the result
    const std::string               paramPrefix;        ///< prefix for a system parameter in the result
    std::string                     defaultUrl;         ///< if no url is specified for EV_URL events
    std::string                     errorString;        ///< error string returned by executed script if any
    std::string                     traceTimestamp;     ///< trace timestamp returned by script if any
    std::string                     failureCause;       ///< where in the process it was failed - mainly intended for system debug
    std::string                     systemParam;        ///< system parameter in the result - typically the activity_log_id
    cURLpp::Cleanup                 cleaner;            ///< object that initialises / cleans up the curl lib
    cURLpp::Easy                    request;            ///< cUrlpp request object
    int                             pid;                ///< process pid
    int                             timeout;            ///< max time
    bool                            bPost;              ///< true to post rather than get
    queueManagementEvent*           pQueueManagement;   ///< class that generates queue management events
    bool                            bParseResponseForObject;  ///< true to try and parse the execution output for an object
};	// class urlRequest

#endif // !defined( urlRequest_defined_)

