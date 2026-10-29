#include "PCH.h"
#include "UI/Rml/RmlSourceEdit.h"
#include <cctype>

namespace
{
    std::string Trim( const std::string& InText )
    {
        const size_t first = InText.find_first_not_of( " \t\r\n" );
        if( first == std::string::npos )
        {
            return {};
        }
        const size_t last = InText.find_last_not_of( " \t\r\n" );
        return InText.substr( first, last - first + 1 );
    }


    // The [start, end] of the start tag holding id="InId" (end is the '>'), skipping comments.
    bool FindTag( const std::string& InSource, const std::string& InId, size_t& OutStart, size_t& OutEnd )
    {
        for( const char quote : { '"', '\'' } )
        {
            const std::string needle = std::string( "id=" ) + quote + InId + quote;
            size_t at = 0;
            while( ( at = InSource.find( needle, at ) ) != std::string::npos )
            {
                // A whole attribute (not data-id=...), inside a tag that isn't a comment.
                const bool wholeName = at > 0 && std::isspace( static_cast<unsigned char>( InSource[at - 1] ) );
                const size_t open = InSource.rfind( '<', at );
                const size_t close = InSource.find( '>', at );
                const size_t commentOpen = InSource.rfind( "<!--", at );
                const size_t commentClose = commentOpen == std::string::npos ? std::string::npos : InSource.find( "-->", commentOpen );
                const bool inComment = commentOpen != std::string::npos && ( commentClose == std::string::npos || commentClose > at );
                if( wholeName && open != std::string::npos && close != std::string::npos && !inComment && InSource.find( '>', open ) == close )
                {
                    OutStart = open;
                    OutEnd = close;
                    return true;
                }
                at += needle.size();
            }
        }
        return false;
    }


    // The style attribute's value span inside [InStart, InEnd]: [OutValueStart, OutValueEnd).
    bool FindStyle( const std::string& InSource, size_t InStart, size_t InEnd, size_t& OutValueStart, size_t& OutValueEnd )
    {
        size_t at = InStart;
        while( ( at = InSource.find( "style=", at ) ) != std::string::npos && at < InEnd )
        {
            if( std::isspace( static_cast<unsigned char>( InSource[at - 1] ) ) && at + 6 < InEnd && ( InSource[at + 6] == '"' || InSource[at + 6] == '\'' ) )
            {
                const char quote = InSource[at + 6];
                const size_t close = InSource.find( quote, at + 7 );
                if( close != std::string::npos && close < InEnd )
                {
                    OutValueStart = at + 7;
                    OutValueEnd = close;
                    return true;
                }
            }
            at += 6;
        }
        return false;
    }
}


bool RmlSourceEdit::SetInlineStyle( const std::string& InSource, const std::string& InId, const std::string& InProperty, const std::string& InValue, std::string& OutSource )
{
    size_t start = 0;
    size_t end = 0;
    if( !FindTag( InSource, InId, start, end ) )
    {
        return false;
    }
    size_t valueStart = 0;
    size_t valueEnd = 0;
    if( !FindStyle( InSource, start, end, valueStart, valueEnd ) )
    {
        if( InValue.empty() )
        {
            OutSource = InSource;
            return true;
        }
        // A new attribute before the tag's '>' (or '/>').
        const size_t insert = InSource[end - 1] == '/' ? end - 1 : end;
        const std::string spacer = std::isspace( static_cast<unsigned char>( InSource[insert - 1] ) ) ? "" : " ";
        OutSource = InSource.substr( 0, insert ) + spacer + "style=\"" + InProperty + ": " + InValue + ";\"" + ( insert != end ? " " : "" ) + InSource.substr( insert );
        return true;
    }
    // Rewrite only the declarations' text: each `name: value;` kept as written but the one edited.
    const std::string style = InSource.substr( valueStart, valueEnd - valueStart );
    std::string rebuilt;
    bool replaced = false;
    size_t at = 0;
    while( at <= style.size() )
    {
        const size_t semicolon = style.find( ';', at );
        const std::string declaration = style.substr( at, semicolon == std::string::npos ? std::string::npos : semicolon - at );
        const size_t colon = declaration.find( ':' );
        if( !Trim( declaration ).empty() )
        {
            if( colon != std::string::npos && Trim( declaration.substr( 0, colon ) ) == InProperty )
            {
                if( !InValue.empty() )
                {
                    const size_t lead = declaration.find_first_not_of( " \t" );
                    rebuilt += declaration.substr( 0, lead ) + InProperty + ": " + InValue + ";";
                }
                replaced = true;
            }
            else
            {
                rebuilt += declaration + ( semicolon != std::string::npos ? ";" : "" );
            }
        }
        else if( semicolon != std::string::npos || !declaration.empty() )
        {
            rebuilt += declaration;
        }
        if( semicolon == std::string::npos )
        {
            break;
        }
        at = semicolon + 1;
    }
    if( !replaced && !InValue.empty() )
    {
        const std::string trimmed = Trim( rebuilt );
        rebuilt = trimmed + ( trimmed.empty() || trimmed.back() == ';' ? "" : ";" ) + ( trimmed.empty() ? "" : " " ) + InProperty + ": " + InValue + ";";
    }
    OutSource = InSource.substr( 0, valueStart ) + rebuilt + InSource.substr( valueEnd );
    return true;
}


std::string RmlSourceEdit::GetInlineStyle( const std::string& InSource, const std::string& InId, const std::string& InProperty )
{
    size_t start = 0;
    size_t end = 0;
    size_t valueStart = 0;
    size_t valueEnd = 0;
    if( !FindTag( InSource, InId, start, end ) || !FindStyle( InSource, start, end, valueStart, valueEnd ) )
    {
        return {};
    }
    const std::string style = InSource.substr( valueStart, valueEnd - valueStart );
    size_t at = 0;
    while( at < style.size() )
    {
        const size_t semicolon = style.find( ';', at );
        const std::string declaration = style.substr( at, semicolon == std::string::npos ? std::string::npos : semicolon - at );
        const size_t colon = declaration.find( ':' );
        if( colon != std::string::npos && Trim( declaration.substr( 0, colon ) ) == InProperty )
        {
            return Trim( declaration.substr( colon + 1 ) );
        }
        if( semicolon == std::string::npos )
        {
            break;
        }
        at = semicolon + 1;
    }
    return {};
}
