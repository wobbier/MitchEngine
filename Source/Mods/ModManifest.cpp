#include "PCH.h"
#include "Mods/ModManifest.h"
#include <cctype>
#include <sstream>


bool SemVer::Parse( const std::string& InText, SemVer& OutVersion )
{
    OutVersion = SemVer();
    size_t at = 0;
    int* parts[] = { &OutVersion.Major, &OutVersion.Minor, &OutVersion.Patch };
    for( int part = 0; part < 3; ++part )
    {
        if( at >= InText.size() || !std::isdigit( static_cast<unsigned char>( InText[at] ) ) )
        {
            return false;   // nothing, or a dot with no number after it ("1.", "1.x")
        }
        int value = 0;
        while( at < InText.size() && std::isdigit( static_cast<unsigned char>( InText[at] ) ) )
        {
            value = value * 10 + ( InText[at] - '0' );
            if( value > 1000000 )
            {
                return false;
            }
            ++at;
        }
        *parts[part] = value;
        if( at < InText.size() && InText[at] == '.' && part < 2 )
        {
            ++at;
            continue;
        }
        // The rest may be a pre-release or build suffix ("-beta", "+abc"); nothing else.
        return at == InText.size() || InText[at] == '-' || InText[at] == '+';
    }
    return true;
}


std::string SemVer::ToString() const
{
    return std::to_string( Major ) + "." + std::to_string( Minor ) + "." + std::to_string( Patch );
}


int SemVer::Compare( const SemVer& InOther ) const
{
    if( Major != InOther.Major ) return Major < InOther.Major ? -1 : 1;
    if( Minor != InOther.Minor ) return Minor < InOther.Minor ? -1 : 1;
    if( Patch != InOther.Patch ) return Patch < InOther.Patch ? -1 : 1;
    return 0;
}


bool VersionRange::Parse( const std::string& InText, VersionRange& OutRange )
{
    OutRange = VersionRange();
    OutRange.Text = InText;
    std::istringstream words( InText );
    std::string word;
    while( words >> word )
    {
        if( word == "*" || word == "x" )
        {
            continue;
        }
        Comparator comparator;
        std::string version = word;
        if( word.rfind( ">=", 0 ) == 0 ) { comparator.Op = 'g'; version = word.substr( 2 ); }
        else if( word.rfind( "<=", 0 ) == 0 ) { comparator.Op = 'l'; version = word.substr( 2 ); }
        else if( word[0] == '>' ) { comparator.Op = '>'; version = word.substr( 1 ); }
        else if( word[0] == '<' ) { comparator.Op = '<'; version = word.substr( 1 ); }
        else if( word[0] == '=' ) { comparator.Op = '='; version = word.substr( 1 ); }
        else if( word[0] == '^' || word[0] == '~' )
        {
            SemVer low;
            if( !SemVer::Parse( word.substr( 1 ), low ) )
            {
                return false;
            }
            SemVer high = low;
            // ~1.2.3 and ~1.2: the next minor. ^1.2.3: the next major; ^0.2.3: the next minor.
            if( word[0] == '~' || low.Major == 0 )
            {
                high = SemVer{ low.Major, low.Minor + 1, 0 };
            }
            else
            {
                high = SemVer{ low.Major + 1, 0, 0 };
            }
            OutRange.m_comparators.push_back( { 'g', low } );
            OutRange.m_comparators.push_back( { '<', high } );
            continue;
        }
        if( !SemVer::Parse( version, comparator.Version ) )
        {
            return false;
        }
        // A bare or "=" version with fewer parts means that prefix: "=1.2" is >=1.2.0 <1.3.0.
        const size_t dots = static_cast<size_t>( std::count( version.begin(), version.end(), '.' ) );
        if( comparator.Op == '=' && dots < 2 )
        {
            const SemVer low = comparator.Version;
            const SemVer high = dots == 0 ? SemVer{ low.Major + 1, 0, 0 } : SemVer{ low.Major, low.Minor + 1, 0 };
            OutRange.m_comparators.push_back( { 'g', low } );
            OutRange.m_comparators.push_back( { '<', high } );
            continue;
        }
        OutRange.m_comparators.push_back( comparator );
    }
    return true;
}


bool VersionRange::Contains( const SemVer& InVersion ) const
{
    for( const Comparator& comparator : m_comparators )
    {
        const int order = InVersion.Compare( comparator.Version );
        const bool holds = comparator.Op == '=' ? order == 0
            : comparator.Op == '<' ? order < 0
            : comparator.Op == '>' ? order > 0
            : comparator.Op == 'l' ? order <= 0
            : order >= 0;
        if( !holds )
        {
            return false;
        }
    }
    return true;
}


bool ModManifest::Parse( const json& InJson, ModManifest& OutManifest, std::string& OutError )
{
    OutManifest = ModManifest();
    if( !InJson.is_object() )
    {
        OutError = "mod.json isn't a JSON object";
        return false;
    }
    OutManifest.Id = InJson.value( "id", std::string() );
    if( OutManifest.Id.empty() || OutManifest.Id.find_first_of( " \t/\\\\" ) != std::string::npos )
    {
        OutError = "\"id\" is missing or has spaces or slashes";
        return false;
    }
    OutManifest.Name = InJson.value( "name", OutManifest.Id );
    const std::string version = InJson.value( "version", std::string( "0.0.0" ) );
    if( !SemVer::Parse( version, OutManifest.Version ) )
    {
        OutError = "\"version\" '" + version + "' isn't a version (like 1.2.0)";
        return false;
    }
    OutManifest.EngineRange = InJson.value( "engine", std::string( "*" ) );
    VersionRange range;
    if( !VersionRange::Parse( OutManifest.EngineRange, range ) )
    {
        OutError = "\"engine\" '" + OutManifest.EngineRange + "' isn't a version range";
        return false;
    }
    if( InJson.contains( "dependencies" ) )
    {
        if( !InJson["dependencies"].is_object() )
        {
            OutError = "\"dependencies\" must map mod ids to version ranges";
            return false;
        }
        for( const auto& [id, value] : InJson["dependencies"].items() )
        {
            const std::string text = value.is_string() ? value.get<std::string>() : std::string( "*" );
            if( !VersionRange::Parse( text, range ) )
            {
                OutError = "dependency '" + id + "': '" + text + "' isn't a version range";
                return false;
            }
            OutManifest.Dependencies[id] = text;
        }
    }
    auto strings = [&InJson]( const char* InKey, std::vector<std::string>& OutList ) {
        if( InJson.contains( InKey ) && InJson[InKey].is_array() )
        {
            for( const json& entry : InJson[InKey] )
            {
                if( entry.is_string() )
                {
                    OutList.push_back( entry.get<std::string>() );
                }
            }
        }
    };
    strings( "loadAfter", OutManifest.LoadAfter );
    strings( "loadBefore", OutManifest.LoadBefore );
    strings( "assemblies", OutManifest.Assemblies );
    OutManifest.NetRequired = InJson.value( "netRequired", false );
    return true;
}
