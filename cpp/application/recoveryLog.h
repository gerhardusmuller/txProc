/**
 recoveryLog - normally used by event to write recovery entries
 
 $Id: recoveryLog.h 2591 2012-09-25 17:43:55Z gerhardus $
 Versioning: a.b.c a is a major release, b represents changes or new features, c represents bug fixes. 
 @version 1.0.0		01/10/2009		Gerhardus Muller		Script created

 @note

 @todo
 
 @bug

	Copyright Notice
 */

#if !defined( recoveryLog_defined_ )
#define recoveryLog_defined_

#include <fstream>
#include "utils/object.h"

class recoveryLog : public object
{
  // Definitions
  public:
    static const char *const recoveryFilebase;
    static const char *const recoveryDirbase;
    static const char *const logDirbase;

    // Methods
  public:
    recoveryLog( const char* baseDir, bool bRotate, const std::string& logrotatePath, const std::string& runAsUser, const std::string& logGroup, int logFilesToKeep );
    virtual ~recoveryLog();
    virtual std::string toString ();

    void reOpen( );
    static void rotate( const char* baseDir, const std::string& logrotatePath, const std::string& runAsUser, const std::string& logGroup, int logFilesToKeep );
    void writeEntry( baseEvent* theEvent, const char* error, const char* from=NULL, const char* to=NULL );
    void initRecovery( const char* fileToRecover, int fd );
    bool recover( );
    int getCountRecoveryLines()           {return countRecoveryLines;}
    void resetCountRecoveryLines()        {countRecoveryLines=0;}

  private:
    void processLine( std::string& line );
    void finishRecovery( );

    // Properties
  public:
    static logger*              pStaticLogger;          ///< class scope logger
    static logger               staticLogger;           ///< class scope logger

  protected:

  private:
    static int                  seq;            ///< sequence number of the events written to the recovery log
    static int                  countRecoveryLines; ///< same as seq but resetable
    int                         count;          ///< count of lines read from the recovery file
    int                         countProcessed; ///< count of lines successfully processed - ie submitted to the dispatcher queue
    int                         countFailed;    ///< count of lines unsuccessfully recovered
    int                         countIgnored;   ///< count of lines ignored
    int                         startTime;      ///< start time of the recovery
    int                         dispatcherFd;   ///< file descriptor for dispatcher when doing recovery
    std::fstream                recStream;      ///< recovery stream
    std::ofstream               ofs;            ///< output stream opened on the recovery log
    std::string                 recoveryDir;    ///< directory for recovery files
    std::string                 logDir;         ///< directory for logs
    std::string                 recoveryLogname;///< name of the recovery log file
};	// class recoveryLog

  
#endif // !defined( recoveryLog_defined_)
