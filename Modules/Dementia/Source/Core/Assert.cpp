#include "Assert.h"

#if USING( ME_PLATFORM_WINDOWS )

#include <windows.h>
#include <DbgHelp.h>
#include <string>
#include <sstream>
#include "Utils\StringUtils.h"
#include <filesystem>

#pragma comment(lib, "Dbghelp.lib")

HHOOK hHook;

LRESULT CALLBACK CBTProc( int nCode, WPARAM wParam, LPARAM lParam )
{
    if( nCode < 0 )
    {
        return CallNextHookEx( hHook, nCode, wParam, lParam );
    }

    if( nCode == HCBT_ACTIVATE )
    {
        HWND hDlg = (HWND)wParam;
        SetDlgItemText( hDlg, IDCANCEL, L"Ignore" );
        SetDlgItemText( hDlg, IDTRYAGAIN, L"Break" );
        SetDlgItemText( hDlg, IDCONTINUE, L"Crash" );
    }

    return 0;
}
void CustomAssertFunction( const char* expression, const char* inMessage, const char* file, int line )
{
    std::wstringstream ws;
    if( inMessage )
    {
        ws << inMessage;
        ws << L"\n\n";
    }

    auto p = std::filesystem::current_path();
    std::string ProgramPath( std::string( p.generic_string() ) );

    std::string fileString( file );
    std::replace( fileString.begin(), fileString.end(), '\\', '/' );
    std::replace( ProgramPath.begin(), ProgramPath.end(), '\\', '/' );

    auto endlmao = fileString.find( ProgramPath );
    if( endlmao != std::string::npos )
    {
        std::string attttttttt = std::string( file );
        ProgramPath = fileString.substr( endlmao, attttttttt.length() );
    }

    ws << L"Assert: ";
    ws << expression;

    ws << L"\n\nFile: ";
    ws << file;
    ws << L":";
    ws << line;

    // Capture the call stack
    ws << L"\n\nCall stack:\n";

    void* stack[100];
    HANDLE process = GetCurrentProcess();
    SymInitialize( process, NULL, TRUE );
    unsigned short frames = CaptureStackBackTrace( 0, 100, stack, NULL );
    SYMBOL_INFO* symbol = (SYMBOL_INFO*)calloc( sizeof( SYMBOL_INFO ) + 256, 1 );
    symbol->MaxNameLen = 255;
    symbol->SizeOfStruct = sizeof( SYMBOL_INFO );

    IMAGEHLP_LINE64 lineInfo = { sizeof( IMAGEHLP_LINE64 ) };
    for( int i = 1; i < frames; i++ )
    {
        DWORD64 address = (DWORD64)( stack[i] );
        SymFromAddr( process, address, 0, symbol );

        DWORD dwDisplacement;
        if( SymGetLineFromAddr64( process, address, &dwDisplacement, &lineInfo ) )
        {
            std::wstring thing = std::wstring( lineInfo.FileName, lineInfo.FileName + strlen( lineInfo.FileName ) );
            std::size_t slash = thing.find_last_of( '\\' );
            std::wstring fileName = thing.substr( slash + 1, thing.length() );
            ws << frames - i - 1 << ": " << symbol->Name << " in (" << fileName << ":" << lineInfo.LineNumber << ")\n\n";
        }
        else
        {
            ws << frames - i - 1 << ": " << symbol->Name << " at (0x" << std::hex << symbol->Address << std::dec << ")\n\n";
        }

        //if( strcmp( symbol->Name, "main" ) == 0 )
        //{
        //    i = frames;
        //    break;
        //}
    }

    free( symbol );
    SymCleanup( process );

    std::wstring message = ws.str();

    hHook = SetWindowsHookEx( WH_CBT, &CBTProc, 0, GetCurrentThreadId() );

    std::wstring title = inMessage ? ( L"Assertion Failed: " + StringUtils::ToWString( inMessage ) ) : L"Assertion Failed";

    int msgboxID = MessageBox(
        NULL,
        message.c_str(),
        title.c_str(),
        MB_ICONWARNING | MB_CANCELTRYCONTINUE | MB_DEFBUTTON2
    );

    switch( msgboxID )
    {
    case IDCANCEL:
        // "Ignore" was clicked, continue execution
        break;
    case IDTRYAGAIN:
        // "Break" was clicked, break into the debugger
        __debugbreak();
        break;
    case IDCONTINUE:
        // "Crash" was clicked, terminate the application
        exit( -1 );
        break;
    }
}

#endif