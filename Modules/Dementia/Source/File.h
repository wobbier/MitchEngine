#pragma once
#include "Path.h"
#include <fstream>
#include "CLog.h"
#include "VFS/VFS.h"

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

        if( !FilePath.Exists )
        {
            YIKES_FMT( "[File IO] File does not exist: %s", FilePath.GetLocalPath().data() );
            return Data;
        }

        if( !VFS::Get().ReadText( FilePath, Data ) )
        {
            YIKES_FMT( "[File IO] Failed to load file: %s", FilePath.GetLocalPath().data() );
            return Data;
        }

        INFO_FMT("[File IO]", "Loaded File : %s", FilePath.GetLocalPathString().c_str() );

        return Data;
    }

    void Append( const std::string& inContents )
    {
        Data.append( inContents );
    }

    void Write()
    {
        // Creates missing parent directories: user:// writes land in dirs that don't exist yet.
        if( !VFS::Get().WriteText( FilePath, Data ) )
        {
            YIKES_FMT( "[File IO] Failed to write file: %s", FilePath.GetLocalPath().data() );
        }
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
};