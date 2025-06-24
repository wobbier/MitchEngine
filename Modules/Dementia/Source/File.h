#pragma once
#include "Path.h"
#include <fstream>
#include "CLog.h"

class File
{
public:
    File() = default;
    File( const Path& path )
        : FilePath( path )
    {
    }

    const std::string& Read()
    {
        if( !Data.empty() )
        {
            return Data;
        }
        std::fstream FileStream;

        if( !FilePath.Exists )
        {
            YIKES_FMT( "[File IO] File does not exist: %s", FilePath.GetLocalPath().data() );
        }

        FileStream.open( FilePath.FullPath.c_str(), std::ios::in );

        if( !FileStream )
        {
            YIKES_FMT( "[File IO] Failed to load file: %s", FilePath.GetLocalPath().data() );
            FileStream.close();

            return Data;
        }

         // #TODO: Perhaps having the macro for this accepts a string view?
        CLog::Log( CLog::LogType::Info, "[File IO] Loaded File: " + FilePath.GetLocalPathString() );

        IsOpen = true;

        Data.assign( ( std::istreambuf_iterator<char>( FileStream ) ), ( std::istreambuf_iterator<char>() ) );

        FileStream.close();
        IsOpen = false;

        return Data;
    }

    void Append( const std::string& inContents )
    {
        Data.append( inContents );
    }

    void Write()
    {
        std::ofstream out( FilePath.FullPath );
        out << Data;
        out.close();
    }

    void Write( const std::string& inContents )
    {
        Data = inContents;
        Write();
    }

    void Reset()
    {
        Data.clear();
    }

    std::string Data;
    Path FilePath;
private:

    bool IsOpen = false;
};