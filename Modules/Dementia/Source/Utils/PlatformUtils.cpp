#include <Utils/PlatformUtils.h>

#if USING( ME_PLATFORM_WIN64 )
#include <processthreadsapi.h>
#endif

#include <filesystem>
#include <chrono>
#include <algorithm>
#include <cstdlib>
#include "File.h"

void PlatformUtils::RunProcess( const Path& inFilePath, const std::string& inArgs /*= ""*/ )
{
#if USING( ME_PLATFORM_WIN64 )
    STARTUPINFO si;
    PROCESS_INFORMATION pi;

    // set the size of the structures
    ZeroMemory( &si, sizeof( si ) );
    si.cb = sizeof( si );
    ZeroMemory( &pi, sizeof( pi ) );

    // start the program up
    CreateProcess( StringUtils::ToWString( inFilePath.FullPath ).c_str(),   // the path
        &StringUtils::ToWString( inArgs )[0],        // Command line
        NULL,           // Process handle not inheritable
        NULL,           // Thread handle not inheritable
        FALSE,          // Set handle inheritance to FALSE
        0,              // No creation flags
        NULL,           // Use parent's environment block
        NULL,           // Use parent's starting directory 
        &si,            // Pointer to STARTUPINFO structure
        &pi             // Pointer to PROCESS_INFORMATION structure (removed extra parentheses)
    );
    // Close process and thread handles. 
    CloseHandle( pi.hProcess );
    CloseHandle( pi.hThread );
#endif
}

void PlatformUtils::SystemCall( const Path& inFilePath, const std::string& inArgs /*= ""*/, bool inRunFromDirectory /*= true*/ )
{
#if USING( ME_PLATFORM_WIN64 )
    auto p = std::filesystem::current_path();
    std::string ProgramPath( std::string( p.generic_string() ) );
    if( inFilePath.IsFile && inRunFromDirectory )
    {
        SetCurrentDirectory( StringUtils::ToWString( inFilePath.GetDirectoryString() ).c_str() );
    }

    STARTUPINFO si;
    PROCESS_INFORMATION pi;
    ZeroMemory( &si, sizeof( si ) );
    si.cb = sizeof( si );
    ZeroMemory( &pi, sizeof( pi ) );

    if( !CreateProcessW( StringUtils::ToWString( inFilePath.FullPath ).c_str(), &StringUtils::ToWString( inArgs )[0], NULL, NULL, FALSE, 0, NULL, NULL, &si, &pi ) )
    {
        printf( "CreateProcess failed (%d).\n", GetLastError() );
        throw std::exception( "Could not create child process" );
    }

    WaitForSingleObject( pi.hProcess, INFINITE );

    CloseHandle( pi.hProcess );
    CloseHandle( pi.hThread );
    SetCurrentDirectory( StringUtils::ToWString( ProgramPath ).c_str() );
#else
    std::string progArgs = "\"" + inFilePath.FullPath + "\" " + inArgs;
    std::system( progArgs.c_str() );
#endif
}

void PlatformUtils::CreateDirectory( const Path& inFilePath )
{
    std::error_code error;
    std::filesystem::create_directories( std::string( inFilePath.GetDirectory() ), error );
}


namespace
{
    // Single-quotes an argument for /bin/sh.
    std::string ShellQuote( const std::string& inText )
    {
        std::string quoted = "'";
        for( char c : inText )
        {
            if( c == '\'' )
            {
                quoted += "'\\''";
            }
            else
            {
                quoted += c;
            }
        }
        return quoted + "'";
    }
}


void PlatformUtils::RunDetached( const std::string& inCommand )
{
#if USING( ME_PLATFORM_WIN64 )
    std::system( ( "start \"\" " + inCommand ).c_str() );
#elif USING( ME_PLATFORM_LINUX ) || USING( ME_PLATFORM_MACOS )
    std::system( ( inCommand + " >/dev/null 2>&1 &" ).c_str() );
#endif
}


void PlatformUtils::OpenFile( const Path& inFilePath )
{
#if USING( ME_PLATFORM_WIN64 )
    ShellExecute( NULL, L"open", StringUtils::ToWString( inFilePath.FullPath ).c_str(), NULL, NULL, SW_SHOWDEFAULT );
#elif USING( ME_PLATFORM_MACOS )
    RunDetached( "open " + ShellQuote( inFilePath.FullPath ) );
#elif USING( ME_PLATFORM_LINUX )
    RunDetached( "xdg-open " + ShellQuote( inFilePath.FullPath ) );
#endif
}


void PlatformUtils::OpenFolder( const Path& inFolderPath )
{
    const std::string folder = inFolderPath.IsFolder ? inFolderPath.FullPath : std::string( inFolderPath.GetDirectory() );
#if USING( ME_PLATFORM_WIN64 )
    ShellExecute( NULL, L"open", StringUtils::ToWString( folder ).c_str(), NULL, NULL, SW_SHOWDEFAULT );
#elif USING( ME_PLATFORM_MACOS )
    RunDetached( "open " + ShellQuote( folder ) );
#elif USING( ME_PLATFORM_LINUX )
    RunDetached( "xdg-open " + ShellQuote( folder ) );
#endif
}


void PlatformUtils::ShowInFileManager( const Path& inFilePath )
{
#if USING( ME_PLATFORM_WIN64 )
    const std::wstring arguments = L"/select,\"" + StringUtils::ToWString( inFilePath.FullPath ) + L"\"";
    ShellExecute( NULL, L"open", L"explorer.exe", arguments.c_str(), NULL, SW_SHOWDEFAULT );
#elif USING( ME_PLATFORM_MACOS )
    RunDetached( "open -R " + ShellQuote( inFilePath.FullPath ) );
#else
    OpenFolder( inFilePath );
#endif
}


namespace
{
    std::string& CodeEditorCommand()
    {
        static std::string command;
        return command;
    }
}


void PlatformUtils::SetCodeEditorCommand( const std::string& inCommand )
{
    CodeEditorCommand() = inCommand;
}


void PlatformUtils::OpenInCodeEditor( const std::string& inFile, int inLine )
{
    const char* custom = std::getenv( "ME_CODE_EDITOR" );
    std::string command = !CodeEditorCommand().empty() ? CodeEditorCommand() : ( custom ? custom : "code -g \"{file}:{line}\"" );
    auto replace = [&command]( const std::string& token, const std::string& value ) {
        for( size_t at = command.find( token ); at != std::string::npos; at = command.find( token, at + value.size() ) )
        {
            command.replace( at, token.size(), value );
        }
    };
    replace( "{file}", inFile );
    replace( "{line}", std::to_string( std::max( inLine, 1 ) ) );
    RunDetached( command );
}

void PlatformUtils::DeleteFile( const Path& inFilePath )
{
    std::filesystem::remove( inFilePath.FullPath );
}


bool PlatformUtils::MoveToTrash( const Path& inFilePath, const std::string& trashDirectory )
{
    namespace fs = std::filesystem;
    std::error_code ec;
    fs::create_directories( trashDirectory, ec );

    const auto stamp = std::chrono::duration_cast<std::chrono::seconds>( std::chrono::system_clock::now().time_since_epoch() ).count();
    auto moveOne = [&]( const fs::path& source ) -> bool
    {
        if( !fs::exists( source, ec ) )
        {
            return true;
        }
        const fs::path destination = fs::path( trashDirectory ) / ( std::to_string( stamp ) + "_" + source.filename().string() );
        fs::rename( source, destination, ec );
        if( ec )
        {
            // Different filesystem: fall back to copy + remove.
            ec.clear();
            fs::copy( source, destination, fs::copy_options::recursive | fs::copy_options::overwrite_existing, ec );
            if( ec )
            {
                return false;
            }
            fs::remove_all( source, ec );
        }
        return !ec;
    };

    const fs::path source( inFilePath.FullPath );
    if( !moveOne( source ) )
    {
        return false;
    }
    return moveOne( fs::path( inFilePath.FullPath + ".meta" ) );
}

Buffer PlatformUtils::ReadBytes( const Path& inFilePath )
{
    // Maybe swap to use file in the future?
    //File path = File( inFilePath );
    //const std::string& p = path.Read();
    std::ifstream stream( inFilePath.FullPath, std::ios::binary | std::ios::ate );

    if( !stream )
    {
        // Failed to open the file
        return {};
    }

    std::streampos end = stream.tellg();
    stream.seekg( 0, std::ios::beg );
    uint32_t size = static_cast<uint32_t>( end - stream.tellg() );

    if( size == 0 )
    {
        // File is empty
        return {};
    }

    Buffer buffer( size );
    stream.read( (char*)buffer.Data, size );
    stream.close();

    return buffer;
}
