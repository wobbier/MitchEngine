#pragma once

#include "StringUtils.h"
#include <Path.h>
#include <Core/Buffer.h>

namespace PlatformUtils
{
    void RunProcess( const Path& inFilePath, const std::string& inArgs = "" );

    void SystemCall( const Path& inFilePath, const std::string& inArgs = "", bool inRunFromDirectory = true );

    // Runs a shell command, waits for it and returns its exit code; stdout and stderr go to OutOutput.
    int RunCommand( const std::string& inCommand, std::string& OutOutput );

    void CreateDirectory( const Path& inFilePath );

    // Opens a file with its default application / a folder in the file manager.
    void OpenFile( const Path& inFilePath );
    void OpenFolder( const Path& inFolderPath );
    // Opens the file manager with the file selected (its folder on Linux).
    void ShowInFileManager( const Path& inFilePath );
    // Opens a source file at a line in the code editor: $ME_CODE_EDITOR ("{file}" and "{line}" are
    // substituted) or VS Code ("code -g file:line").
    void OpenInCodeEditor( const std::string& inFile, int inLine );
    // Overrides the code editor command template (empty = $ME_CODE_EDITOR or VS Code).
    void SetCodeEditorCommand( const std::string& inCommand );
    // Starts a shell command without waiting for it.
    void RunDetached( const std::string& inCommand );

    void DeleteFile( const Path& inFilePath );

    // Moves a file (and its .meta sidecar, if any) into trashDirectory with a timestamped name.
    // Recoverable alternative to DeleteFile for editor operations. Returns false on failure.
    bool MoveToTrash( const Path& inFilePath, const std::string& trashDirectory );


    Buffer ReadBytes( const Path& inFilePath );
}
