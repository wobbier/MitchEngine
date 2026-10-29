#include "PCH.h"
#include "UI/Rml/RmlSourceEdit.h"
#include <algorithm>
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

    bool InComment( const std::string& InSource, size_t InAt )
    {
        const size_t commentOpen = InSource.rfind( "<!--", InAt );
        if( commentOpen == std::string::npos )
        {
            return false;
        }
        const size_t commentClose = InSource.find( "-->", commentOpen );
        return commentClose == std::string::npos || commentClose > InAt;
    }


    std::string TagName( const std::string& InSource, size_t InStart )
    {
        size_t end = InStart + 1;
        while( end < InSource.size() && !std::isspace( static_cast<unsigned char>( InSource[end] ) ) && InSource[end] != '>' && InSource[end] != '/' )
        {
            ++end;
        }
        return InSource.substr( InStart + 1, end - InStart - 1 );
    }


    // The element's matching end tag: [OutCloseStart, OutCloseEnd] ('<' of "</name" to its '>').
    // Self-closing elements end at their start tag. Nested elements of the same name are counted.
    bool FindEndTag( const std::string& InSource, size_t InStart, size_t InEnd, size_t& OutCloseStart, size_t& OutCloseEnd )
    {
        if( InSource[InEnd - 1] == '/' )
        {
            OutCloseStart = InStart;
            OutCloseEnd = InEnd;
            return true;
        }
        const std::string name = TagName( InSource, InStart );
        int depth = 1;
        size_t at = InEnd + 1;
        while( ( at = InSource.find( '<', at ) ) != std::string::npos )
        {
            if( InSource.compare( at, 4, "<!--" ) == 0 )
            {
                const size_t close = InSource.find( "-->", at );
                if( close == std::string::npos )
                {
                    return false;
                }
                at = close + 3;
                continue;
            }
            const size_t close = InSource.find( '>', at );
            if( close == std::string::npos )
            {
                return false;
            }
            if( InSource[at + 1] == '/' )
            {
                if( TagName( InSource, at + 1 ) == name && --depth == 0 )
                {
                    OutCloseStart = at;
                    OutCloseEnd = close;
                    return true;
                }
            }
            else if( TagName( InSource, at ) == name && InSource[close - 1] != '/' )
            {
                ++depth;
            }
            at = close + 1;
        }
        return false;
    }


    // Where InAt's line starts, and whether only whitespace comes before InAt on it.
    size_t LineStart( const std::string& InSource, size_t InAt, bool& OutOnlyWhitespace )
    {
        const size_t newline = InAt == 0 ? std::string::npos : InSource.rfind( '\n', InAt - 1 );
        const size_t start = newline == std::string::npos ? 0 : newline + 1;
        OutOnlyWhitespace = InSource.find_first_not_of( " \t", start ) >= InAt;
        return start;
    }


    std::string Newline( const std::string& InSource )
    {
        return InSource.find( "\r\n" ) != std::string::npos ? "\r\n" : "\n";
    }


    std::string CollapseWhitespace( const std::string& InText )
    {
        std::string out;
        bool space = false;
        for( const char c : Trim( InText ) )
        {
            if( std::isspace( static_cast<unsigned char>( c ) ) )
            {
                space = true;
                continue;
            }
            if( space )
            {
                out += ' ';
                space = false;
            }
            out += c;
        }
        return out;
    }


    // An RCSS top-level rule: its block's '{' and '}'.
    bool FindRule( const std::string& InSource, const std::string& InSelector, size_t& OutOpen, size_t& OutClose )
    {
        const std::string wanted = CollapseWhitespace( InSelector );
        size_t selectorStart = 0;
        size_t at = 0;
        while( at < InSource.size() )
        {
            if( InSource.compare( at, 2, "/*" ) == 0 )
            {
                const size_t close = InSource.find( "*/", at + 2 );
                if( close == std::string::npos )
                {
                    return false;
                }
                // A comment before a selector isn't part of it.
                if( Trim( InSource.substr( selectorStart, at - selectorStart ) ).empty() )
                {
                    selectorStart = close + 2;
                }
                at = close + 2;
                continue;
            }
            if( InSource[at] == '{' )
            {
                // The block's end, past nested blocks (@media, @keyframes) and comments.
                int depth = 1;
                size_t end = at + 1;
                for( ; end < InSource.size() && depth > 0; ++end )
                {
                    if( InSource.compare( end, 2, "/*" ) == 0 )
                    {
                        const size_t close = InSource.find( "*/", end + 2 );
                        end = close == std::string::npos ? InSource.size() : close + 1;
                        continue;
                    }
                    depth += InSource[end] == '{' ? 1 : InSource[end] == '}' ? -1 : 0;
                }
                if( depth != 0 )
                {
                    return false;
                }
                if( CollapseWhitespace( InSource.substr( selectorStart, at - selectorStart ) ) == wanted )
                {
                    OutOpen = at;
                    OutClose = end - 1;
                    return true;
                }
                at = end;
                selectorStart = end;
                continue;
            }
            ++at;
        }
        return false;
    }


    // A declaration inside a block (or an inline style): the name's start, the value's span
    // (trimmed) and the declaration's end (its ';', or InEnd when the last one has none).
    struct Declaration
    {
        size_t NameStart = 0;
        size_t ValueStart = 0;
        size_t ValueEnd = 0;
        size_t End = 0;
    };


    bool FindDeclaration( const std::string& InSource, size_t InStart, size_t InEnd, const std::string& InProperty, Declaration& OutDeclaration )
    {
        size_t at = InStart;
        while( at < InEnd )
        {
            if( InSource.compare( at, 2, "/*" ) == 0 )
            {
                const size_t close = InSource.find( "*/", at + 2 );
                at = close == std::string::npos || close > InEnd ? InEnd : close + 2;
                continue;
            }
            if( std::isspace( static_cast<unsigned char>( InSource[at] ) ) || InSource[at] == ';' )
            {
                ++at;
                continue;
            }
            // One declaration: up to its ';' (or the block's end).
            size_t end = InSource.find( ';', at );
            end = end == std::string::npos || end > InEnd ? InEnd : end;
            const size_t colon = InSource.find( ':', at );
            if( colon != std::string::npos && colon < end && Trim( InSource.substr( at, colon - at ) ) == InProperty )
            {
                OutDeclaration.NameStart = at;
                OutDeclaration.ValueStart = InSource.find_first_not_of( " \t\r\n", colon + 1 );
                size_t valueEnd = end;
                while( valueEnd > colon + 1 && std::isspace( static_cast<unsigned char>( InSource[valueEnd - 1] ) ) )
                {
                    --valueEnd;
                }
                OutDeclaration.ValueStart = std::min( OutDeclaration.ValueStart, valueEnd );
                OutDeclaration.ValueEnd = valueEnd;
                OutDeclaration.End = end;
                return true;
            }
            at = end + 1;
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


bool RmlSourceEdit::InsertChild( const std::string& InSource, const std::string& InParentId, const std::string& InMarkup, std::string& OutSource )
{
    size_t start = 0;
    size_t end = 0;
    size_t closeStart = 0;
    size_t closeEnd = 0;
    if( !FindTag( InSource, InParentId, start, end ) || InSource[end - 1] == '/' || !FindEndTag( InSource, start, end, closeStart, closeEnd ) )
    {
        return false;
    }
    const std::string newline = Newline( InSource );
    bool parentOwnLine = false;
    const size_t parentLine = LineStart( InSource, start, parentOwnLine );
    const std::string parentIndent = parentOwnLine ? InSource.substr( parentLine, start - parentLine ) : std::string();
    // The children's indentation: the first child line's, else one more tab (or 4 spaces).
    std::string childIndent = parentIndent + ( parentIndent.find( ' ' ) != std::string::npos ? "    " : "\t" );
    const size_t firstLine = InSource.find( '\n', end );
    if( firstLine != std::string::npos && firstLine < closeStart )
    {
        const size_t content = InSource.find_first_not_of( " \t\r\n", firstLine + 1 );
        if( content != std::string::npos && content < closeStart )
        {
            bool ownLine = false;
            const size_t line = LineStart( InSource, content, ownLine );
            childIndent = InSource.substr( line, content - line );
        }
    }
    bool closeOwnLine = false;
    const size_t closeLine = LineStart( InSource, closeStart, closeOwnLine );
    if( closeOwnLine && closeLine > end )
    {
        OutSource = InSource.substr( 0, closeLine ) + childIndent + InMarkup + newline + InSource.substr( closeLine );
    }
    else
    {
        // <div id="a"></div> (or children on the tag's line): open it up onto lines.
        OutSource = InSource.substr( 0, closeStart ) + newline + childIndent + InMarkup + newline + parentIndent + InSource.substr( closeStart );
    }
    return true;
}


bool RmlSourceEdit::RemoveElement( const std::string& InSource, const std::string& InId, std::string& OutSource )
{
    size_t start = 0;
    size_t end = 0;
    size_t closeStart = 0;
    size_t closeEnd = 0;
    if( !FindTag( InSource, InId, start, end ) || !FindEndTag( InSource, start, end, closeStart, closeEnd ) )
    {
        return false;
    }
    bool ownLine = false;
    const size_t line = LineStart( InSource, start, ownLine );
    size_t after = closeEnd + 1;
    const size_t rest = InSource.find_first_not_of( " \t\r", after );
    if( ownLine && ( rest == std::string::npos || InSource[rest] == '\n' ) )
    {
        // The whole line(s), newline included.
        OutSource = InSource.substr( 0, line ) + ( rest == std::string::npos ? std::string() : InSource.substr( rest + 1 ) );
        return true;
    }
    OutSource = InSource.substr( 0, start ) + InSource.substr( after );
    return true;
}


std::string RmlSourceEdit::UniqueId( const std::string& InSource, const std::string& InBase )
{
    size_t start = 0;
    size_t end = 0;
    if( !FindTag( InSource, InBase, start, end ) )
    {
        return InBase;
    }
    for( int i = 2;; ++i )
    {
        const std::string id = InBase + std::to_string( i );
        if( !FindTag( InSource, id, start, end ) )
        {
            return id;
        }
    }
}


std::vector<std::string> RmlSourceEdit::GetStyleSheetLinks( const std::string& InSource )
{
    std::vector<std::string> links;
    size_t at = 0;
    while( ( at = InSource.find( "<link", at ) ) != std::string::npos )
    {
        const size_t close = InSource.find( '>', at );
        if( close == std::string::npos )
        {
            break;
        }
        const std::string tag = InSource.substr( at, close - at );
        const size_t href = tag.find( "href=" );
        if( !InComment( InSource, at ) && tag.find( "rcss" ) != std::string::npos && href != std::string::npos && href + 5 < tag.size() )
        {
            const char quote = tag[href + 5];
            const size_t valueEnd = tag.find( quote, href + 6 );
            if( ( quote == '"' || quote == '\'' ) && valueEnd != std::string::npos )
            {
                links.push_back( tag.substr( href + 6, valueEnd - href - 6 ) );
            }
        }
        at = close;
    }
    return links;
}


bool RmlSourceEdit::SetRuleProperty( const std::string& InSource, const std::string& InSelector, const std::string& InProperty, const std::string& InValue, std::string& OutSource )
{
    const std::string newline = Newline( InSource );
    size_t open = 0;
    size_t close = 0;
    if( !FindRule( InSource, InSelector, open, close ) )
    {
        OutSource = InSource;
        if( !InValue.empty() )
        {
            const bool endsWithNewline = !InSource.empty() && InSource.back() == '\n';
            OutSource += ( InSource.empty() || endsWithNewline ? "" : newline ) + ( InSource.empty() ? "" : newline ) + CollapseWhitespace( InSelector ) + " {" + newline + "\t" + InProperty + ": " + InValue + ";" + newline + "}" + newline;
        }
        return true;
    }
    Declaration declaration;
    if( FindDeclaration( InSource, open + 1, close, InProperty, declaration ) )
    {
        if( !InValue.empty() )
        {
            OutSource = InSource.substr( 0, declaration.ValueStart ) + InValue + InSource.substr( declaration.ValueEnd );
            return true;
        }
        // Removed with its line when it had one to itself.
        bool ownLine = false;
        const size_t line = LineStart( InSource, declaration.NameStart, ownLine );
        size_t after = declaration.End < close ? declaration.End + 1 : close;
        const size_t rest = InSource.find_first_not_of( " \t\r", after );
        if( ownLine && rest != std::string::npos && InSource[rest] == '\n' )
        {
            OutSource = InSource.substr( 0, line ) + InSource.substr( rest + 1 );
        }
        else
        {
            const size_t next = InSource.find_first_not_of( " \t", after );
            OutSource = InSource.substr( 0, declaration.NameStart ) + InSource.substr( next == std::string::npos || next > close ? after : next );
        }
        return true;
    }
    if( InValue.empty() )
    {
        OutSource = InSource;
        return true;
    }
    // A new declaration at the end of the block: the last line's indentation on multi-line
    // blocks, inline on one-line ones. The previous declaration gets its ';' if it had none.
    size_t last = InSource.find_last_not_of( " \t\r\n", close - 1 );
    const bool needsSemicolon = last > open && InSource[last] != ';' && InSource.compare( last - 1, 2, "*/" ) != 0;
    const std::string semicolon = needsSemicolon ? ";" : "";
    bool closeOwnLine = false;
    const size_t closeLine = LineStart( InSource, close, closeOwnLine );
    if( closeOwnLine && closeLine > open )
    {
        std::string indent = "\t";
        if( last > open )
        {
            bool ownLine = false;
            const size_t lastLine = LineStart( InSource, last, ownLine );
            const size_t content = InSource.find_first_not_of( " \t", lastLine );
            if( lastLine > open )
            {
                indent = InSource.substr( lastLine, content - lastLine );
            }
        }
        OutSource = InSource.substr( 0, last + 1 ) + semicolon + InSource.substr( last + 1, closeLine - last - 1 ) + indent + InProperty + ": " + InValue + ";" + newline + InSource.substr( closeLine );
        return true;
    }
    OutSource = InSource.substr( 0, last + 1 ) + semicolon + " " + InProperty + ": " + InValue + "; " + InSource.substr( close );
    return true;
}


std::string RmlSourceEdit::GetRuleProperty( const std::string& InSource, const std::string& InSelector, const std::string& InProperty )
{
    size_t open = 0;
    size_t close = 0;
    Declaration declaration;
    if( !FindRule( InSource, InSelector, open, close ) || !FindDeclaration( InSource, open + 1, close, InProperty, declaration ) )
    {
        return {};
    }
    return InSource.substr( declaration.ValueStart, declaration.ValueEnd - declaration.ValueStart );
}


bool RmlSourceEdit::HasRule( const std::string& InSource, const std::string& InSelector )
{
    size_t open = 0;
    size_t close = 0;
    return FindRule( InSource, InSelector, open, close );
}
