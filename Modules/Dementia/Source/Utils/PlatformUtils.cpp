#include <Utils/PlatformUtils.h>

#if USING( ME_PLATFORM_WIN64 )
#include <processthreadsapi.h>
#include <psapi.h>
#endif

#include <filesystem>
#include <chrono>
#include <algorithm>
#include <cstdlib>
#include <cstdio>
#include "File.h"

#if USING( ME_PLATFORM_LINUX ) || USING( ME_PLATFORM_MACOS )
#include <sys/wait.h>
#include <unistd.h>
#endif

#if USING( ME_PLATFORM_MACOS )
#include <mach/mach.h>
#endif

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

int PlatformUtils::RunCommand( const std::string& inCommand, std::string& OutOutput )
{
    OutOutput.clear();
    return RunCommand( inCommand, [&OutOutput]( const std::string& inLine ) {
        OutOutput += inLine;
        OutOutput += '\n';
    } );
}


int PlatformUtils::RunCommand( const std::string& inCommand, const std::function<void( const std::string& )>& inOnLine )
{
    const std::string command = inCommand + " 2>&1";
#if USING( ME_PLATFORM_WIN64 )
    FILE* pipe = _popen( command.c_str(), "r" );
#else
    FILE* pipe = popen( command.c_str(), "r" );
#endif
    if( !pipe )
    {
        return -1;
    }
    char buffer[512];
    std::string line;
    while( fgets( buffer, sizeof( buffer ), pipe ) )
    {
        line += buffer;
        if( !line.empty() && line.back() == '\n' )
        {
            line.pop_back();
            if( !line.empty() && line.back() == '\r' )
            {
                line.pop_back();
            }
            inOnLine( line );
            line.clear();
        }
    }
    if( !line.empty() )
    {
        inOnLine( line );
    }
#if USING( ME_PLATFORM_WIN64 )
    return _pclose( pipe );
#else
    const int status = pclose( pipe );
    return WIFEXITED( status ) ? WEXITSTATUS( status ) : -1;
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


void PlatformUtils::OpenFolderInCodeEditor( const std::string& inFolder )
{
    const char* custom = std::getenv( "ME_CODE_EDITOR" );
    const std::string editorTemplate = !CodeEditorCommand().empty() ? CodeEditorCommand() : ( custom ? custom : "code" );
    // The template opens file:line references; a workspace open only wants its command word.
    const std::string editor = editorTemplate.substr( 0, editorTemplate.find( ' ' ) );
    RunDetached( ( editor.empty() ? std::string( "code" ) : editor ) + " " + ShellQuote( inFolder ) );
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


uint64_t PlatformUtils::GetProcessMemoryBytes()
{
#if USING( ME_PLATFORM_WIN64 )
    PROCESS_MEMORY_COUNTERS counters{};
    counters.cb = sizeof( counters );
    // The kernel32 export (Windows 7+), so nothing extra links in.
    return K32GetProcessMemoryInfo( GetCurrentProcess(), &counters, sizeof( counters ) ) ? static_cast<uint64_t>( counters.WorkingSetSize ) : 0;
#elif USING( ME_PLATFORM_MACOS )
    mach_task_basic_info_data_t info{};
    mach_msg_type_number_t count = MACH_TASK_BASIC_INFO_COUNT;
    return task_info( mach_task_self(), MACH_TASK_BASIC_INFO, reinterpret_cast<task_info_t>( &info ), &count ) == KERN_SUCCESS ? static_cast<uint64_t>( info.resident_size ) : 0;
#elif USING( ME_PLATFORM_LINUX )
    // statm: total and resident sizes in pages.
    FILE* statm = std::fopen( "/proc/self/statm", "r" );
    if( !statm )
    {
        return 0;
    }
    unsigned long long pages = 0, resident = 0;
    const bool read = std::fscanf( statm, "%llu %llu", &pages, &resident ) == 2;
    std::fclose( statm );
    return read ? static_cast<uint64_t>( resident ) * static_cast<uint64_t>( sysconf( _SC_PAGESIZE ) ) : 0;
#else
    return 0;
#endif
}
