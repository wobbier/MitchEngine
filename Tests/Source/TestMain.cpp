#define DOCTEST_CONFIG_IMPLEMENT
#include <doctest/doctest.h>

#include "CLog.h"
#include "Core/CommandLine.h"
#include "Engine/Project.h"

int main( int argc, char** argv )
{
    CommandLine::Set( argc, argv );
    // The VFS roots, as every executable sets them: the working directory and its engine (a
    // game checkout's Engine/, or an engine checkout itself).
    if( !Project::InitFromCommandLine() )
    {
        return 1;
    }

    // Keep test output readable: only warnings and errors from the engine reach the console.
    CLog::GetInstance().SetLogVerbosity( CLog::LogType::Warning );

    doctest::Context context;
    context.applyCommandLine( argc, argv );
    return context.run();
}
