#pragma once
#include <cstdlib>
#include <regex>
#include <string>

// The first source reference in a log message, for the Log panel's click-to-open:
// "path/file.ext:line", "path/file.ext(line,column)" (compilers, dotnet build) or
// "path/file.ext:line N" (.NET stack traces).
inline bool FindSourceLocation( const std::string& InText, std::string& OutFile, int& OutLine )
{
    static const std::regex pattern( R"((([A-Za-z]:)?[\w./\\-]+\.(cpp|cc|c|hpp|h|inl|cs|sc|sh|lvl|prefab|json))(:line |:|\()(\d+))" );
    std::smatch match;
    if( !std::regex_search( InText, match, pattern ) )
    {
        return false;
    }
    OutFile = match.str( 1 );
    OutLine = std::atoi( match.str( 5 ).c_str() );
    return true;
}
