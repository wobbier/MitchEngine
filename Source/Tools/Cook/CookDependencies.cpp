#include "PCH.h"
#include "Tools/Cook/CookDependencies.h"

#include "CLog.h"
#include "Core/CommandLine.h"
#include "JSON.h"
#include "Path.h"
#include "VFS/VFS.h"

#include <filesystem>
#include <fstream>

namespace Cook
{
    void StartRecordingIfRequested()
    {
        if( CommandLine::Has( "--record-deps" ) )
        {
            VFS::Get().SetRecordingReads( true );
        }
    }


    void WriteRecordingIfRequested()
    {
        const std::string file = CommandLine::GetString( "--record-deps" );
        if( file.empty() )
        {
            return;
        }
        const std::vector<std::string> reads = VFS::Get().GetRecordedReads();
        if( WriteDependencies( file, reads ) )
        {
            CLog::Log( CLog::LogType::Info, "Recorded " + std::to_string( reads.size() ) + " dependencies to " + file );
        }
        else
        {
            CLog::Log( CLog::LogType::Error, "Couldn't write the recorded dependencies to " + file );
        }
    }


    bool WriteDependencies( const std::string& InFile, const std::vector<std::string>& InReads )
    {
        json out;
        out["Version"] = 1;
        out["Reads"] = InReads;
        std::error_code error;
        const std::filesystem::path parent = std::filesystem::path( InFile ).parent_path();
        if( !parent.empty() )
        {
            std::filesystem::create_directories( parent, error );
        }
        std::ofstream stream( InFile );
        stream << out.dump( 1 ) << "\n";
        return stream.good();
    }


    bool ReadDependencies( const std::string& InFile, std::vector<std::string>& OutReads )
    {
        std::string text;
        if( !VFS::Get().ReadText( Path( InFile, true ), text ) )
        {
            return false;
        }
        const json in = json::parse( text, nullptr, false );
        if( !in.is_object() || !in.contains( "Reads" ) || !in["Reads"].is_array() )
        {
            return false;
        }
        for( const json& read : in["Reads"] )
        {
            if( read.is_string() )
            {
                OutReads.push_back( read.get<std::string>() );
            }
        }
        return true;
    }
}
