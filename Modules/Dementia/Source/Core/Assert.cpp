#include "Assert.h"

#if USING( ME_PLATFORM_WINDOWS ) && !USING( ME_RETAIL )

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

bool CustomAssertFunction( const char* expression, const char* inMessage, const char* file, int line )
{
    std::wstringstream assertOutput;
    if( inMessage )
    {
        assertOutput << inMessage;
        assertOutput << L"\n\n";
    }

    assertOutput << L"Assert: ";
    assertOutput << expression;

    assertOutput << L"\n\nFile: ";
    assertOutput << file;
    assertOutput << L":";
    assertOutput << line;

    // Capture the call stack
    assertOutput << L"\n\nCall stack:\n";

    void* stack[100];
    HANDLE process = GetCurrentProcess();
    SymInitialize( process, NULL, TRUE );
    unsigned short frames = CaptureStackBackTrace( 0, 100, stack, NULL );
    SYMBOL_INFO* stackSymbols = (SYMBOL_INFO*)calloc( sizeof( SYMBOL_INFO ) + 256, 1 );
    stackSymbols->MaxNameLen = 255;
    stackSymbols->SizeOfStruct = sizeof( SYMBOL_INFO );

    IMAGEHLP_LINE64 lineInfo = { sizeof( IMAGEHLP_LINE64 ) };
    for( int i = 1; i < frames; i++ )
    {
        DWORD64 address = (DWORD64)( stack[i] );
        SymFromAddr( process, address, 0, stackSymbols );

        DWORD dwDisplacement;
        if( SymGetLineFromAddr64( process, address, &dwDisplacement, &lineInfo ) )
        {
            std::wstring thing = std::wstring( lineInfo.FileName, lineInfo.FileName + strlen( lineInfo.FileName ) );
            std::size_t slash = thing.find_last_of( '\\' );
            std::wstring fileName = thing.substr( slash + 1, thing.length() );
            assertOutput << frames - i - 1 << ": " << stackSymbols->Name << " in (" << fileName << ":" << lineInfo.LineNumber << ")\n\n";
        }
        else
        {
            assertOutput << frames - i - 1 << ": " << stackSymbols->Name << " at (0x" << std::hex << stackSymbols->Address << std::dec << ")\n\n";
        }
    }

    free( stackSymbols );
    SymCleanup( process );

    std::wstring message = assertOutput.str();

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
        return true;
    case IDCONTINUE:
        // "Crash" was clicked, terminate the application
        exit( -1 );
        break;
    }
    return false;
}

#endif