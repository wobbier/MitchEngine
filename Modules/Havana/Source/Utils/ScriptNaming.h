#pragma once
#include <algorithm>
#include <cctype>
#include <functional>
#include <regex>
#include <string>

// Names for C# script files. Game.Script compiles every .cs under Assets into one assembly and the
// engine finds script classes by name, so a script's file name, class name and uniqueness across
// the project have to agree, or the whole script build fails.
namespace ScriptNaming
{
    inline bool IsKeyword( const std::string& InName )
    {
        static const char* kKeywords[] = { "abstract", "as", "base", "bool", "break", "byte", "case", "catch", "char", "checked",
            "class", "const", "continue", "decimal", "default", "delegate", "do", "double", "else", "enum", "event", "explicit",
            "extern", "false", "finally", "fixed", "float", "for", "foreach", "goto", "if", "implicit", "in", "int", "interface",
            "internal", "is", "lock", "long", "namespace", "new", "null", "object", "operator", "out", "override", "params",
            "private", "protected", "public", "readonly", "ref", "return", "sbyte", "sealed", "short", "sizeof", "stackalloc",
            "static", "string", "struct", "switch", "this", "throw", "true", "try", "typeof", "uint", "ulong", "unchecked",
            "unsafe", "ushort", "using", "virtual", "void", "volatile", "while" };
        return std::find( std::begin( kKeywords ), std::end( kKeywords ), InName ) != std::end( kKeywords );
    }


    // A C# identifier (ASCII): a letter or underscore, then letters, digits or underscores, and no keyword.
    inline bool IsIdentifier( const std::string& InName )
    {
        if( InName.empty() || std::isdigit( static_cast<unsigned char>( InName[0] ) ) )
        {
            return false;
        }
        for( char c : InName )
        {
            if( !std::isalnum( static_cast<unsigned char>( c ) ) && c != '_' )
            {
                return false;
            }
        }
        return !IsKeyword( InName );
    }


    // The closest identifier to a typed name: "my cool-script" becomes "MyCoolScript", "2D Player" becomes "_2DPlayer".
    inline std::string MakeIdentifier( const std::string& InName )
    {
        std::string result;
        bool upperNext = true;
        for( char c : InName )
        {
            if( std::isalnum( static_cast<unsigned char>( c ) ) || c == '_' )
            {
                result += upperNext ? static_cast<char>( std::toupper( static_cast<unsigned char>( c ) ) ) : c;
                upperNext = false;
            }
            else
            {
                upperNext = true;
            }
        }
        if( result.empty() )
        {
            return "NewScript";
        }
        if( std::isdigit( static_cast<unsigned char>( result[0] ) ) || IsKeyword( result ) )
        {
            result = "_" + result;
        }
        return result;
    }


    // InBase, or InBase2, InBase3... the first name InTaken says is free.
    inline std::string UniqueName( const std::string& InBase, const std::function<bool( const std::string& )>& InTaken )
    {
        std::string name = InBase;
        for( int i = 2; InTaken( name ); ++i )
        {
            name = InBase + std::to_string( i );
        }
        return name;
    }


    // Renames the type declared as InFrom (class, struct or record) to InTo. False if there was none.
    inline bool RenameType( std::string& InOutSource, const std::string& InFrom, const std::string& InTo )
    {
        if( !IsIdentifier( InFrom ) || !IsIdentifier( InTo ) )
        {
            return false;
        }
        const std::regex declaration( "\\b(class|struct|record)(\\s+)" + InFrom + "\\b" );
        if( !std::regex_search( InOutSource, declaration ) )
        {
            return false;
        }
        InOutSource = std::regex_replace( InOutSource, declaration, "$1$2" + InTo );
        return true;
    }


    // Puts [ScriptCore.FormerName("InFormerName")] on the line declaring InTypeName, so scenes that
    // still name the old class keep loading it (and save the new name). False if there's no such declaration.
    inline bool AddFormerName( std::string& InOutSource, const std::string& InTypeName, const std::string& InFormerName )
    {
        if( !IsIdentifier( InTypeName ) || !IsIdentifier( InFormerName ) )
        {
            return false;
        }
        const std::regex declaration( "(^|\\n)([ \\t]*)[^\\n]*\\b(class|struct|record)\\s+" + InTypeName + "\\b" );
        std::smatch match;
        if( !std::regex_search( InOutSource, match, declaration ) )
        {
            return false;
        }
        const std::string newline = InOutSource.find( "\r\n" ) != std::string::npos ? "\r\n" : "\n";
        const size_t lineStart = static_cast<size_t>( match.position( 0 ) + match.length( 1 ) );
        InOutSource.insert( lineStart, match.str( 2 ) + "[ScriptCore.FormerName(\"" + InFormerName + "\")]" + newline );
        return true;
    }
}
