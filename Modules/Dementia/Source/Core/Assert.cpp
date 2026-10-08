#include "Assert.h"

#if !USING( ME_RETAIL )

#include "CLog.h"
#include "Core/CommandLine.h"
#include "Core/StackTrace.h"
#include <cstdlib>
#include <string>

#if USING( ME_PLATFORM_WIN64 )
#include <windows.h>
#include "Utils/StringUtils.h"

namespace
{
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
}
#endif


bool CustomAssertFunction( const char* expression, const char* inMessage, const char* file, int line, bool* ignoreAlways )
{
    std::string report = "Assertion failed: ";
    report += expression;
    if( inMessage )
    {
        report += "\n\t";
        report += inMessage;
    }
    report += "\n\t";
    report += file;
    report += ":";
    report += std::to_string( line );
    report += "\nCall stack:\n";
    report += StackTrace::CaptureString( 1 );

    CLog::Log( CLog::LogType::Error, report );
    CLog::GetInstance().Flush();

    if( CommandLine::Has( "--assert-fatal" ) )
    {
        std::abort();
    }

#if USING( ME_PLATFORM_WIN64 )
    if( CommandLine::Has( "--assert-log" ) )
    {
        return Debugger::IsAttached();
    }

    const std::wstring message = StringUtils::ToWString( report );
    const std::wstring title = inMessage ? ( L"Assertion Failed: " + StringUtils::ToWString( inMessage ) ) : L"Assertion Failed";

    hHook = SetWindowsHookEx( WH_CBT, &CBTProc, 0, GetCurrentThreadId() );
    const int msgboxID = MessageBox( NULL, message.c_str(), title.c_str(), MB_ICONWARNING | MB_CANCELTRYCONTINUE | MB_DEFBUTTON2 );
    UnhookWindowsHookEx( hHook );

    switch( msgboxID )
    {
    case IDTRYAGAIN:
        return true;
    case IDCONTINUE:
        std::exit( -1 );
    case IDCANCEL:
    default:
        return false;
    }
#else
    if( Debugger::IsAttached() )
    {
        return true;
    }

    // No debugger to stop in: report each assert site once and keep running.
    if( ignoreAlways )
    {
        *ignoreAlways = true;
    }
    return false;
#endif
}

#endif
