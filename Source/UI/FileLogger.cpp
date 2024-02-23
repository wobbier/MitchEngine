#include "PCH.h"
#include "FileLogger.h"

#if USING( ME_UI )

#include <iostream>
#include "CLog.h"

namespace ultralight {

    FileLogger::FileLogger( const String& log_path ) : log_file_( log_path.utf8().data() ) {
        if( !log_file_.is_open() ) {
            std::cerr << "Could not open log file for writing with path: " <<
                log_path.utf8().data() << std::endl;
        }
    }

    FileLogger::~FileLogger() {
    }

    void FileLogger::LogMessage( LogLevel log_level, const String& message ) {
        if( !log_file_.is_open() )
            return;

        log_file_ << "> " << String( message ).utf8().data() << std::endl << std::endl;

        switch( log_level )
        {
        case ultralight::LogLevel::Error:
            YIKES( message.utf8().data() );
            break;
        case ultralight::LogLevel::Warning:
            BRUH( message.utf8().data() );
            break;
        case ultralight::LogLevel::Info:
            CLog::Log( CLog::LogType::Info, message.utf8().data() );
            break;
        default:
            break;
        }
    }

}  // namespace ultralight

#endif