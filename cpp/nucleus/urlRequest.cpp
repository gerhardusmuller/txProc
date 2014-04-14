/** @class urlRequest
 urlRequest - curl requests
 
 $Id: urlRequest.cpp 2549 2012-09-03 19:20:15Z gerhardus $
 Versioning: a.b.c a is a major release, b represents changes or new features, c represents bug fixes. 
 @version 1.0.0		30/09/2009		Gerhardus Muller		Script created
 @version 1.0.1		24/01/2011		Gerhardus Muller		bPost flag on curlPP not reset after a POST request
 @version 1.1.0		11/02/2011		Gerhardus Muller		moved parsing of result into try/catch and added catching of json runtime errors
 @version 1.2.0		30/08/2012		Gerhardus Muller		made provision for a default url and queue management events

 @note

 @todo
 
 @bug

	Copyright Notice
 */

#include "nucleus/urlRequest.h"
#include "nucleus/baseEvent.h"
#include "nucleus/optionsNucleus.h"
#include "nucleus/queueManagementEvent.h"

#define HAVE_CONFIG_H   // require for curlpp to compile
#include <curlpp/Options.hpp>
#include <curlpp/Exception.hpp>
#include <curlpp/Infos.hpp>
#include "boost/regex.hpp"

#ifdef CURLL_NAMESPACE_FIX
namespace curlpp = cURLpp;
#endif
//namespace cURLpp  // gerhardus - seeming gcc4 requires this ..
//{
//  template< >
//    void
//    cURLpp::InfoTypeConverter< long >::get(cURLpp::Easy &handle, 
//        CURLINFO info,
//        long &value)
//    {
//      long tmp;
//      InfoGetter::get(handle, info, tmp);
//      value = tmp;
//    }
//}

/**
 Construction
 */
urlRequest::urlRequest( int thePid, const std::string& urlSuc, const std::string& urlFail, const std::string& errorPref, const std::string& tracePref, const std::string& paramPref, int theTimeout, const std::string& theQueue, bool theParseResponseForObject, const std::string& theDefaultUrl )
  : object( "urlRequest" ),
    urlSuccess(urlSuc), 
    urlFailure(urlFail), 
    errorPrefix(errorPref), 
    tracePrefix(tracePref), 
    paramPrefix(paramPref),
    defaultUrl(theDefaultUrl),
    timeout(theTimeout),
    bParseResponseForObject( theParseResponseForObject )
{
  char tmp[64];
  sprintf( tmp, "urlRequest-%s", theQueue.c_str() );
  log.setInstanceName( tmp );
  pid = thePid;
  bPost = false;
  pQueueManagement = NULL;

  // Set the writer callback to enable cURL to write result in a memory area
  cURLpp::Types::WriteFunctionFunctor functor( this, &urlRequest::writeMemoryCallback);
  cURLpp::Options::WriteFunction *writeFuncOption = new cURLpp::Options::WriteFunction( functor );
  request.setOpt( writeFuncOption );
  log.setAddPid( true );
}	// urlRequest

/**
 Destruction
 */
urlRequest::~urlRequest()
{
}	// ~urlRequest

/**
 Standard logging call - produces a generic text version of the urlRequest.
 Memory allocation / deleting is handled by this urlRequest.
 @return pointer to a string describing the state of the urlRequest.  The string has an unspecified lifetime and is not multi-thread safe
 */
std::string urlRequest::toString( )
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
void urlRequest::parseStandardResponse( bool& bSuccess, const std::string& result )
{
  // only try and parse the result: if the script was successfully executed
  // i.e. did not fail all together (at least got a proper HTTP response)
  if( bSuccess )
  {
    bool bFoundSuccess = false;
    bool bFoundFail = false;
    bFoundSuccess = (result.find( urlSuccess ) != std::string::npos );
    bFoundFail = (result.find( urlFailure ) != std::string::npos );
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
    std::string regStr = errorPrefix + "([^\n]*)";
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
 * process a http request event - url escapes the parameters
 * @param pEvent
 * @param result - optional result string
 * @param pResult - optional result object
 * @return true if success
 * **/
bool urlRequest::process( baseEvent* pEvent, std::string& result, baseEvent* &pResult ) 
{
  bool bSuccess = true;
  char linkChar;
  pResult = NULL;

  bPost = false;
  url = pEvent->getUrl();
  // if no url is defined and we have a default url use that
  if( (url.empty() || (url.length()==0)) && (defaultUrl.length()>0) ) url = defaultUrl;
  builtUrl = url;
  urlResult.erase( );
  errorString.erase();
  traceTimestamp.erase();
  systemParam.erase();
  failureCause.erase();
  std::string encodedParams;

  try
  {
    linkChar = (url.find( '?' ) == std::string::npos) ? '?' : '&';   // the initial linkChar depends on if the url already contains a ?

    // add the name/value pairs
    Json::ValueConstIterator paramPair = pEvent->paramBegin();
    std::string name;
    std::string value;
    for( unsigned int i = 0; i < pEvent->scriptParamSize(); i++ )
    {
      Json::Value jKey = paramPair.key();
      Json::Value jVal = (*paramPair);
      if( !jKey.isString() || !jVal.isString() )
        throw Exception( log, log.WARN, "process: parameters have to be strings:'%s'", pEvent->toString().c_str() );
      name = cURLpp::escape( jKey.asCString() );
      value = cURLpp::escape( jVal.asCString() );
      if( i > 0 )
        encodedParams += '&' + name + "=" + value;
      else
        encodedParams = name + "=" + value;
      paramPair++;
    } // for

    // Setting the URL to retrieve.
    // full option docs at http://curl.haxx.se/libcurl/c/curl_easy_setopt.html
    // curlpp header include/curlpp/Options.hpp
    builtUrl += linkChar + encodedParams;
    if( builtUrl.length() > pOptionsNucleus->maxGetRequestLength )
      bPost = true;
    if( bPost )
      request.setOpt( new cURLpp::Options::Url(url) );
    else
      request.setOpt( new cURLpp::Options::Url(builtUrl) );
    //    request.setOpt( new cURLpp::Options::Verbose( true ) );

    //CURLOPT_TIMEOUT 
    // Pass a long as parameter containing the maximum time in seconds 
    // that you allow the libcurl transfer operation to take. Normally, 
    // name lookups can take a considerable time and limiting operations 
    // to less than a few minutes risk aborting perfectly normal operations. 
    // This option will cause curl to use the SIGALRM to enable time-outing 
    // system calls. In unix-like systems, this might cause signals to be 
    // used unless CURLOPT_NOSIGNAL is set.
    if( timeout > 0 ) request.setOpt( new curlpp::Options::Timeout(timeout) );

    // use post method if requested
    if( bPost )
    {
      request.setOpt( new curlpp::Options::Post(1) );
      request.setOpt(new curlpp::Options::PostFields(encodedParams));
    } // if bPost
    else
      request.setOpt( new curlpp::Options::Post(0) );

    // uses the easy interface
    request.perform();

    // retrieve the http response code
    cURLpp::Infos::ResponseCode::get( request, responseCode );
    if( ( responseCode > 210 ) || ( responseCode < 200 ) )
    {
      bSuccess = false;

      std::ostringstream oss;
      oss << "responseCode=" << responseCode;
      failureCause = oss.str();
    }

    // string result of the call
    result = urlResult;

    // try to deserialise the output of the script
    if( bSuccess && bParseResponseForObject && (result.length()>(baseEvent::FRAME_HEADER_LEN+baseEvent::BLOCK_HEADER_LEN)) )
      pResult = baseEvent::unSerialiseFromString( result );
    if( (pResult!=NULL) && (pResult->getType()==baseEvent::EV_RESULT) )
      bSuccess = pResult->isSuccess();

    if( (pResult==NULL) && pEvent->getStandardResponse() )
      parseStandardResponse( bSuccess, result );
  } // try
  catch ( cURLpp::LogicError& e ) 
  {
    log.error( "process failed: %s result:'%s'", e.what(),result.c_str() );
    failureCause = e.what();
    bSuccess = false;
  }
  catch( cURLpp::RuntimeError& e )
  {
    log.error( "process failed: %s result:'%s'", e.what(),result.c_str() );
    failureCause = e.what();
    result = "exception: ";
    result += e.what();
    bSuccess = false;
  } // catch
  catch( std::runtime_error e )
  { // json-cpp throws runtime_error
    log.error( "process failed caught std::runtime_error:'%s' result:'%s'", e.what(),result.c_str() );
    failureCause = e.what();
    result = "exception: ";
    result += e.what();
    bSuccess = false;
  } // catch
  catch( ... )
  {
    log.error( "process failed: caught unknown exception result:'%s'",result.c_str() );
    failureCause = "unknown exception";
    result = "exception: unknown";
    bSuccess = false;
  } // catch

  if( log.wouldLog( log.MIDLEVEL ) )
  {
    std::string queue = pEvent->getDestQueue();
    std::ostringstream oss;
    oss << "urlCall success: " << bSuccess << " queue: '" << queue << "'";
    oss << " ref:'" << pEvent->getRef() << "'";
    oss << (bPost?" POST":" GET");
    if( errorString.length() > 0 ) { oss << " error: " << errorString; }
    if( traceTimestamp.length() > 0 ) { oss << " traceT: " << traceTimestamp; }
    if( systemParam.length() > 0 ) { oss << " param: " << systemParam; }
    if( failureCause.length() > 0 ) { oss << " failCause: " << failureCause; }
    oss << " called: " << builtUrl;
    if( !pEvent->getTrace().empty() ) { oss << " traceB|:" << pEvent->getTrace() << ":|traceE,"; }
    if( pResult != NULL )
      oss << " deserialised output: " << pResult->toString();
    else
    {
      if( !result.empty() )
        oss << " result:" << result;
      else
        oss << " empty result";
    } // else
    if( !bSuccess ) { oss << " event: " << pEvent->toString(); }
    log.info( log.MIDLEVEL ) << oss.str();
  } // if
  return bSuccess;
} // process

/**
 Checks if a character requires translation
 */
bool urlRequest::bNeedTranslation( const char c )
{
  static char xlation[] = "<>.#{}|\\^~[]`+/?& ";
  char* x = xlation;
  while( *x != '\0' )
  {
    if( *x == c ) return true;
    x++;
  }
  return false;
} // bNeedTranslation

/**
 Basic URL encoding
 */
void urlRequest::urlEncode( std::string& encoded, const char* unencoded )
{
  char str[5];
  encoded = "";
  int len = strlen( unencoded );
  for( int i = 0; i < len; i++ )
  {
    if( bNeedTranslation( unencoded[i] ) )
    {
      sprintf( str, "%%%02X", (char)unencoded[i] );
    }
    else
    {
      str[0] = unencoded[i];
      str[1] = '\0';
    }
    encoded = encoded + str;
  } // for
} // urlEncode

/**
 * cUrlpp write callback function
 * @param ptr - pointer to the received data
 * @param size - number of elements
 * @param nmemb - size of an element
 * **/
size_t  urlRequest::writeMemoryCallback(char* ptr, size_t size, size_t nmemb)
{
  size_t realsize = size * nmemb;
  urlResult.append( ptr, realsize );
  return realsize;
} // writeMemoryCallback
