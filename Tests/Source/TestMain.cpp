#define DOCTEST_CONFIG_IMPLEMENT
#include <doctest/doctest.h>

#include "CLog.h"
#include "Core/CommandLine.h"

int main( int argc, char** argv )
{
    CommandLine::Set( argc, argv );

    // Keep test output readable: only warnings and errors from the engine reach the console.
    CLog::GetInstance().SetLogVerbosity( CLog::LogType::Warning );

    doctest::Context context;
    context.applyCommandLine( argc, argv );
    return context.run();
}
