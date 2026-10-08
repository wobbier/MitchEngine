#include "CommandLine.h"
#include <cstdlib>


void CommandLine::Set( int argc, char** argv )
{
    std::vector<std::string>& args = Args();
    args.clear();
    for( int i = 0; i < argc; ++i )
    {
        args.emplace_back( argv[i] ? argv[i] : "" );
    }
}


bool CommandLine::Has( const std::string& name )
{
    return FindIndex( name ) >= 0;
}


std::string CommandLine::GetString( const std::string& name, const std::string& defaultValue )
{
    const std::vector<std::string>& args = Args();
    const int index = FindIndex( name );
    if( index < 0 || index + 1 >= static_cast<int>( args.size() ) )
    {
        return defaultValue;
    }

    const std::string& value = args[index + 1];
    if( value.rfind( "--", 0 ) == 0 )
    {
        return defaultValue;
    }
    return value;
}


int CommandLine::GetInt( const std::string& name, int defaultValue )
{
    const std::string value = GetString( name );
    return value.empty() ? defaultValue : std::atoi( value.c_str() );
}


float CommandLine::GetFloat( const std::string& name, float defaultValue )
{
    const std::string value = GetString( name );
    return value.empty() ? defaultValue : static_cast<float>( std::atof( value.c_str() ) );
}


const std::vector<std::string>& CommandLine::GetArgs()
{
    return Args();
}


std::vector<std::string>& CommandLine::Args()
{
    static std::vector<std::string> args;
    return args;
}


int CommandLine::FindIndex( const std::string& name )
{
    const std::vector<std::string>& args = Args();
    for( size_t i = 1; i < args.size(); ++i )
    {
        if( args[i] == name )
        {
            return static_cast<int>( i );
        }
    }
    return -1;
}
