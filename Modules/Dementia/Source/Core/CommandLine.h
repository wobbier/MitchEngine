#pragma once
#include <string>
#include <vector>

// Process-wide command line access. Populated once from main() via ME_APPLICATION_MAIN.
// Arguments are of the form "--name value" or "--flag".
class CommandLine
{
public:
    static void Set( int argc, char** argv );

    static bool Has( const std::string& name );
    static std::string GetString( const std::string& name, const std::string& defaultValue = "" );
    static int GetInt( const std::string& name, int defaultValue = 0 );
    static float GetFloat( const std::string& name, float defaultValue = 0.f );

    static const std::vector<std::string>& GetArgs();

private:
    static std::vector<std::string>& Args();
    static int FindIndex( const std::string& name );
};
