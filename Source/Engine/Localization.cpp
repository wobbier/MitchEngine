#include "PCH.h"
#include "Localization.h"
#include "CLog.h"
#include "VFS/VFS.h"
#include <algorithm>

namespace
{
    // Read in order; a later file's key wins.
    std::vector<std::string> TablePaths( const std::string& InCode )
    {
        return { "Engine/Assets/Localization/" + InCode + ".json", "Assets/Localization/" + InCode + ".json", "Assets/UI/Strings/" + InCode + ".json" };
    }


    void MergeTable( const std::string& InPath, std::map<std::string, std::string>& InOutTable )
    {
        std::string text;
        if( !VFS::Get().ReadText( InPath, text ) )
        {
            return;
        }
        const json root = json::parse( text, nullptr, false );
        if( !root.is_object() )
        {
            YIKES( "Localization: " + InPath + " isn't a { \"key\": \"text\" } table" );
            return;
        }
        for( const auto& [key, value] : root.items() )
        {
            if( value.is_string() )
            {
                InOutTable[key] = value.get<std::string>();
            }
        }
    }


    std::string Trimmed( const std::string& InText, size_t& OutFirst, size_t& OutLast )
    {
        OutFirst = InText.find_first_not_of( " \t\r\n" );
        if( OutFirst == std::string::npos )
        {
            return {};
        }
        OutLast = InText.find_last_not_of( " \t\r\n" );
        return InText.substr( OutFirst, OutLast - OutFirst + 1 );
    }


    // Prose has a letter and isn't a data binding or markup (RmlUi also hands SetInnerRML's whole
    // string to the hook before parsing it into text nodes, which come through on their own).
    bool LooksLikeProse( const std::string& InText )
    {
        if( InText.find( "{{" ) != std::string::npos || InText.find( '<' ) != std::string::npos )
        {
            return false;
        }
        return std::any_of( InText.begin(), InText.end(), []( char c ) { return ( c >= 'a' && c <= 'z' ) || ( c >= 'A' && c <= 'Z' ); } );
    }


    // Accented look-alikes of a-z and A-Z (UTF-8), in the Latin Extended range common fonts carry.
    const char* const kPseudoLower[26] = { "á", "ƀ", "ç", "đ", "é", "ƒ", "ĝ", "ĥ", "î", "ĵ", "ķ", "ĺ", "ɱ", "ñ", "ö", "þ", "ǫ", "ŕ", "š", "ţ", "û", "ṽ", "ŵ", "ẋ", "ý", "ž" };
    const char* const kPseudoUpper[26] = { "Å", "Ɓ", "Ç", "Đ", "É", "Ƒ", "Ĝ", "Ĥ", "Î", "Ĵ", "Ķ", "Ĺ", "Ṁ", "Ñ", "Ö", "Þ", "Ǫ", "Ŕ", "Š", "Ţ", "Û", "Ṽ", "Ŵ", "Ẋ", "Ý", "Ž" };
}


Localization& Localization::Get()
{
    static Localization instance;
    return instance;
}


std::map<std::string, std::string> Localization::LoadTable( const std::string& InCode )
{
    std::map<std::string, std::string> table;
    if( InCode == kPseudoLocale )
    {
        for( const auto& [key, text] : LoadTable( kBaseLanguage ) )
        {
            table[key] = Pseudo( text );
        }
        return table;
    }
    for( const std::string& path : TablePaths( InCode ) )
    {
        MergeTable( path, table );
    }
    return table;
}


std::string Localization::Pseudo( const std::string& InText )
{
    std::string out = "[";
    int depth = 0;
    size_t letters = 0;
    for( const char c : InText )
    {
        if( c == '{' )
        {
            ++depth;
        }
        if( depth == 0 && c >= 'a' && c <= 'z' )
        {
            out += kPseudoLower[c - 'a'];
            ++letters;
        }
        else if( depth == 0 && c >= 'A' && c <= 'Z' )
        {
            out += kPseudoUpper[c - 'A'];
            ++letters;
        }
        else
        {
            out += c;
        }
        if( c == '}' && depth > 0 )
        {
            --depth;
        }
    }
    // Translations run longer than English: about 30% more, so layouts get tested for it.
    const size_t padding = ( letters * 3 + 9 ) / 10;
    if( padding > 0 )
    {
        out += " " + std::string( padding, '~' );
    }
    return out + "]";
}


void Localization::SetLanguage( const std::string& InCode )
{
    m_language = InCode;
    m_table = InCode.empty() ? std::map<std::string, std::string>() : LoadTable( InCode );
    m_missing.clear();
    if( !InCode.empty() && m_table.empty() )
    {
        YIKES( "Localization: no strings for language '" + InCode + "'" );
    }
}


std::vector<std::string> Localization::GetLanguages() const
{
    std::set<std::string> codes = { kPseudoLocale };
    for( const char* folder : { "Assets/Localization", "Assets/UI/Strings", "Engine/Assets/Localization" } )
    {
        VFS::Get().Enumerate( folder, false, [&codes]( const std::string& InPath, bool InIsDirectory ) {
            const size_t slash = InPath.find_last_of( '/' );
            const std::string file = InPath.substr( slash == std::string::npos ? 0 : slash + 1 );
            if( !InIsDirectory && file.size() > 5 && file.compare( file.size() - 5, 5, ".json" ) == 0 )
            {
                codes.insert( file.substr( 0, file.size() - 5 ) );
            }
        } );
    }
    return { codes.begin(), codes.end() };
}


std::string Localization::GetLanguageName( const std::string& InCode ) const
{
    if( InCode == kPseudoLocale )
    {
        return "Pseudo";
    }
    const std::map<std::string, std::string> table = LoadTable( InCode );
    const auto name = table.find( "language.name" );
    return name != table.end() ? name->second : InCode;
}


std::string Localization::Get( const std::string& InKey )
{
    const auto found = m_table.find( InKey );
    if( found != m_table.end() )
    {
        return found->second;
    }
    if( !m_language.empty() && LooksLikeProse( InKey ) && m_data.count( InKey ) == 0 )
    {
        m_missing.insert( InKey );
    }
    return InKey;
}


bool Localization::Has( const std::string& InKey ) const
{
    return m_table.count( InKey ) != 0;
}


bool Localization::Translate( const std::string& InText, std::string& OutText )
{
    OutText = InText;
    if( m_language.empty() )
    {
        return false;
    }
    size_t first = 0;
    size_t last = 0;
    const std::string key = Trimmed( InText, first, last );
    if( key.empty() )
    {
        return false;
    }
    const auto found = m_table.find( key );
    if( found == m_table.end() )
    {
        if( LooksLikeProse( key ) && m_data.count( key ) == 0 )
        {
            m_missing.insert( key );
        }
        return false;
    }
    OutText = InText.substr( 0, first ) + found->second + InText.substr( last + 1 );
    return true;
}


void Localization::MarkData( const std::string& InText )
{
    size_t first = 0;
    size_t last = 0;
    const std::string key = Trimmed( InText, first, last );
    if( !key.empty() )
    {
        m_data.insert( key );
        m_missing.erase( key );
    }
}


bool Localization::SaveProjectTable( const std::string& InCode, const std::map<std::string, std::string>& InTable )
{
    json root = json::object();
    for( const auto& [key, text] : InTable )
    {
        root[key] = text;
    }
    return VFS::Get().WriteText( "Assets/Localization/" + InCode + ".json", root.dump( 4 ) );
}
