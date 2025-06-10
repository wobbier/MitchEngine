#pragma once
#include <string>
#include <memory>
#include <Core/Memory.h>
#include "Dementia.h"

#if USING( ME_PLATFORM_WINDOWS )
#include <Windows.h>
#endif

// why is this a class?
class StringUtils
{
public:
#if USING( ME_PLATFORM_WINDOWS )
    static std::string ToString( const std::wstring_view& wstr )
    {
        const int bufferSize = WideCharToMultiByte( CP_UTF8, 0, wstr.data(), -1, nullptr, 0, nullptr, nullptr );
        const std::unique_ptr<char[]> buffer( ME_NEW char[bufferSize] );
        WideCharToMultiByte( CP_UTF8, 0, wstr.data(), -1, buffer.get(), bufferSize, nullptr, nullptr );
        return std::string( buffer.get() );
    }

    static std::wstring ToWString( const std::string_view& str )
    {
        const int bufferSize = MultiByteToWideChar( CP_UTF8, 0, str.data(), -1, nullptr, 0 );
        const std::unique_ptr<wchar_t[]> buffer( ME_NEW wchar_t[bufferSize] );
        MultiByteToWideChar( CP_UTF8, 0, str.data(), -1, buffer.get(), bufferSize );
        return std::wstring( buffer.get() );
    }
#endif // #if USING( ME_PLATFORM_WINDOWS )

    static std::string TrimLeadingSpaces( const std::string& str )
    {
        std::string result = str;
        result.erase( result.begin(), std::find_if( result.begin(), result.end(), []( unsigned char ch ) {
            return !std::isspace( ch );
            } ) );
        return result;
    }
};
