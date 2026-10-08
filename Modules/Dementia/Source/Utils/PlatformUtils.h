#pragma once

#include "StringUtils.h"
#include <Path.h>
#include <Core/Buffer.h>

namespace PlatformUtils
{
    void RunProcess( const Path& inFilePath, const std::string& inArgs = "" );

    void SystemCall( const Path& inFilePath, const std::string& inArgs = "", bool inRunFromDirectory = true );

    void CreateDirectory( const Path& inFilePath );

    void OpenFile( const Path& inFilePath );
    void OpenFolder( const Path& inFolderPath );

    void DeleteFile( const Path& inFilePath );

    // Moves a file (and its .meta sidecar, if any) into trashDirectory with a timestamped name.
    // Recoverable alternative to DeleteFile for editor operations. Returns false on failure.
    bool MoveToTrash( const Path& inFilePath, const std::string& trashDirectory );


    Buffer ReadBytes( const Path& inFilePath );
}
