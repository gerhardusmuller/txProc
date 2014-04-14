/** 
  loggerDefs package

  $Id: loggerDefs.h 1245 2011-02-09 12:06:12Z gerhardus $
  Versioning: a.b.c  a is a major release, b represents changes or new additions, c is a bug fix
  @Version 1.0.0   25/03/2008    Gerhardus Muller     Script created
  @Version 1.1.0   29/01/2009    Gerhardus Muller     Support for a defaultLogLevel that can be updated globally but overridden per instance
  @Version 1.1.1   09/02/2011    Gerhardus Muller     wouldLog's logic was wrong the moment logLevel!=USEDEFAULT

  @note

  @todo

  @bug

  Copyright notice
*/

#ifndef __class_loggerDefs_has_been_included__
#define __class_loggerDefs_has_been_included__

class loggerDefs
{
  public:
    // declarations
    // priority simply signifies a label that is prepended to the log string
    typedef enum { ERROR=0, WARN=1, INFO=2, DEBUG=3 } priority;
    // loglevel allows selective logging - MINLEVEL always logs, MAXLEVEL seldom logs
    typedef enum { USEDEFAULT=0,LOGALWAYS=1,MINLEVEL=1,LEVEL2=2,LOGMOSTLY=3,LOWLEVEL=3,LEVEL4=4,MIDLEVEL=5,LOGNORMAL=5,LEVEL6=6,HIGHLEVEL=7,LOGONOCCASION=7,LEVEL8=8,LEVEL9=9,MAXLEVEL=10,LOGSELDOM=10 } eLogLevel;
    // insert a ENDLINE/EOL to flush the output stream
    typedef enum { ENDLINE=0,EOL=1 } Modifier;

    // methods
    inline bool wouldLog( eLogLevel theLevel )           {if( !((logLevel!=USEDEFAULT)&&(theLevel>logLevel)) && !((logLevel==USEDEFAULT)&&(theLevel>defaultLogLevel)) ) return true; else return false;}
    inline eLogLevel getLogLevel( )                      {return logLevel;}
    void setNewLevel( eLogLevel newLevel )               {logLevel=newLevel;if(logLevel<MINLEVEL)logLevel=MINLEVEL;if(logLevel>MAXLEVEL)logLevel=MAXLEVEL;}
    void setDefaultLevel( eLogLevel newLevel )           {defaultLogLevel=newLevel;if(defaultLogLevel<MINLEVEL)defaultLogLevel=MINLEVEL;if(defaultLogLevel>MAXLEVEL)defaultLogLevel=MAXLEVEL;}

  protected:
    // properties
    eLogLevel             logLevel;                           ///< any messages with level less than this will always be logged
    static eLogLevel      defaultLogLevel;                    ///< default log level - loggerDefs::MAXLEVEL will log everything
};  // loggerDefs

#endif  // #ifndef __class_loggerDefs_has_been_included__

