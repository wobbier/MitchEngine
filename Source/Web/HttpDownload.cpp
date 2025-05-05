#include "HttpDownload.h"
#include "Dementia.h"

#if USING( ME_PLATFORM_WINDOWS )
#include <windows.h>
#include <winhttp.h>
#endif

#if USING( ME_PLATFORM_MACOS )
#include <curl/curl.h>
#endif

#include <iostream>
#include <fstream>
#include "Utils/StringUtils.h"

namespace Web
{
#if USING( ME_PLATFORM_WINDOWS )
    bool DownloadFile( const std::string& inURL, const Path& outPath )
    {
        std::string fileServer, filePath;
        bool isSecure = false;

        if( !SplitUrl( inURL, fileServer, filePath, isSecure ) )
        {
            YIKES_FMT( "Invalid URL: %s", inURL.c_str() );
            return false;
        }

        // Initialize WinHTTP session
        HINTERNET hSession = WinHttpOpen( L"A WinHTTP Example/1.0",
            WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
            WINHTTP_NO_PROXY_NAME,
            WINHTTP_NO_PROXY_BYPASS, 0 );

        if( !hSession )
        {
            YIKES( "Failed to open WinHTTP session." );
            return false;
        }

        // Specify the server to connect to
        INTERNET_PORT port = isSecure ? INTERNET_DEFAULT_HTTPS_PORT : INTERNET_DEFAULT_HTTP_PORT;
        HINTERNET hConnect = WinHttpConnect( hSession, StringUtils::ToWString( fileServer ).c_str(),
            port, 0 );

        if( !hConnect )
        {
            YIKES( "Failed to connect to the server." );
            WinHttpCloseHandle( hSession );
            return false;
        }

        DWORD requestFlags = isSecure ? WINHTTP_FLAG_SECURE : 0;
        // Create an HTTP request handle
        HINTERNET hRequest = WinHttpOpenRequest( hConnect, L"GET", StringUtils::ToWString( filePath ).c_str(),
            NULL, WINHTTP_NO_REFERER,
            WINHTTP_DEFAULT_ACCEPT_TYPES,
            requestFlags );

        if( !hRequest )
        {
            YIKES( "Failed to open HTTP request." );
            WinHttpCloseHandle( hConnect );
            WinHttpCloseHandle( hSession );
            return false;
        }

        // #TODO: Should I gate this on a flag?
        DWORD dwFlags = SECURITY_FLAG_IGNORE_UNKNOWN_CA |
            SECURITY_FLAG_IGNORE_CERT_DATE_INVALID |
            SECURITY_FLAG_IGNORE_CERT_CN_INVALID |
            SECURITY_FLAG_IGNORE_CERT_WRONG_USAGE;
        WinHttpSetOption( hRequest, WINHTTP_OPTION_SECURITY_FLAGS, &dwFlags, sizeof( dwFlags ) );

        // Send the HTTP request
        if( !WinHttpSendRequest( hRequest, WINHTTP_NO_ADDITIONAL_HEADERS, 0,
            WINHTTP_NO_REQUEST_DATA, 0, 0, 0 ) )
        {
            DWORD error = GetLastError();
            YIKES_FMT( "Failed to send request. Error code: %d", error );
            WinHttpCloseHandle( hRequest );
            WinHttpCloseHandle( hConnect );
            WinHttpCloseHandle( hSession );

            return false;
        }

        // Receive the response
        if( !WinHttpReceiveResponse( hRequest, NULL ) )
        {
            YIKES( "Failed to receive response." );

            WinHttpCloseHandle( hRequest );
            WinHttpCloseHandle( hConnect );
            WinHttpCloseHandle( hSession );

            return false;
        }

        DWORD statusCode = 0;
        DWORD statusCodeSize = sizeof( statusCode );

        // Check the status code
        WinHttpQueryHeaders( hRequest,
            WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
            WINHTTP_HEADER_NAME_BY_INDEX,
            &statusCode,
            &statusCodeSize,
            WINHTTP_NO_HEADER_INDEX );

        if( statusCode != 200 )
        {
            YIKES_FMT( "Recieved non-success status code: %d", statusCode );

            WinHttpCloseHandle( hRequest );
            WinHttpCloseHandle( hConnect );
            WinHttpCloseHandle( hSession );

            return false;
        }

        if( !std::filesystem::exists( outPath.GetDirectoryString() ) )
        {
            std::filesystem::create_directories( outPath.GetDirectoryString() );
        }

        // Read the data
        DWORD dwSize = 0;
        DWORD dwDownloaded = 0;
        std::ofstream outfile( outPath.FullPath.c_str(), std::ios::binary );
        if( outfile.is_open() )
        {
            do
            {
            // Check for available data
                dwSize = 0;
                WinHttpQueryDataAvailable( hRequest, &dwSize );

                if( dwSize == 0 )
                    break;

                // Allocate buffer for data
                char* buffer = new char[dwSize];

                // Read data
                if( WinHttpReadData( hRequest, (LPVOID)buffer, dwSize, &dwDownloaded ) )
                {
                    outfile.write( buffer, dwDownloaded );
                }

                delete[] buffer;
            } while( dwSize > 0 );

            outfile.close();
        }
        else
        {
            YIKES( "Failed to open output file." );
        }

        // Clean up
        WinHttpCloseHandle( hRequest );
        WinHttpCloseHandle( hConnect );
        WinHttpCloseHandle( hSession );

        BRUH( "File downloaded successfully." );
        return true;
    }
#endif

    bool SplitUrl( std::string url, std::string& baseUrl, std::string& path, bool& isSecure )
    {
        size_t protocolEnd = url.find( "://" );
        std::string protocol;

        if( protocolEnd == std::string::npos )
        {
            // If no protocol is found, assume http by default
            protocol = "http";
            isSecure = false;
        }
        else
        {
            protocol = url.substr( 0, protocolEnd );
            isSecure = ( protocol == "https" ); // Set isSecure to true if protocol is https
        }

        // Extract the domain and path
        size_t domainStart = ( protocolEnd == std::string::npos ) ? 0 : protocolEnd + 3;
        size_t domainEnd = url.find( '/', domainStart );

        if( domainEnd == std::string::npos )
        {
            baseUrl = url.substr( domainStart ); // URL has no path
            path = "/"; // Root path
            return true;
        }

        baseUrl = url.substr( domainStart, domainEnd - domainStart ); // Extract domain
        path = url.substr( domainEnd ); // Extract path

        // Ensure path starts with a slash
        if( !path.empty() && path[0] != '/' )
        {
            path = "/" + path;
        }

        return true;
    }

#if USING( ME_PLATFORM_MACOS )
    size_t WriteToFileCallback( char* ptr, size_t size, size_t nmemb, void* userdata )
    {
        // 'userdata' is our ofstream pointer
        std::ofstream* outFile = reinterpret_cast<std::ofstream*>( userdata );

        // Write 'size * nmemb' bytes from 'ptr' into the file
        outFile->write( ptr, static_cast<std::streamsize>( size * nmemb ) );

        // Return the number of bytes actually handled
        return size * nmemb;
    }

    bool DownloadFile( const std::string& inURL, const Path& outPath )
    {
        // Ensure directory exists (similar logic to your WinHTTP version)
        std::string directory = outPath.GetDirectoryString();
        if( !directory.empty() )
        {
            if( !std::filesystem::exists( directory ) )
            {
                std::filesystem::create_directories( directory );
            }
        }

        // Initialize libcurl
        CURL* curlHandle = curl_easy_init();
        if( !curlHandle )
        {
            std::cerr << "Failed to initialize libcurl." << std::endl;
            return false;
        }

        // Configure URL
        curl_easy_setopt( curlHandle, CURLOPT_URL, inURL.c_str() );

        // If you need HTTPS ignoring invalid certificates (similar to your WinHTTP code),
        // you can disable cert verification here.
        // **WARNING**: This is generally not recommended in production.
        // Uncomment these lines if your environment requires it:
        //
        // curl_easy_setopt(curlHandle, CURLOPT_SSL_VERIFYPEER, 0L);
        // curl_easy_setopt(curlHandle, CURLOPT_SSL_VERIFYHOST, 0L);

        // Setup write callback
        std::ofstream outfile( outPath.FullPath, std::ios::binary );
        if( !outfile.is_open() )
        {
            std::cerr << "Failed to open output file: " << outPath.FullPath << std::endl;
            curl_easy_cleanup( curlHandle );
            return false;
        }

        curl_easy_setopt( curlHandle, CURLOPT_WRITEFUNCTION, WriteToFileCallback );
        curl_easy_setopt( curlHandle, CURLOPT_WRITEDATA, &outfile );

        // (Optional) Follow redirects if needed
        curl_easy_setopt( curlHandle, CURLOPT_FOLLOWLOCATION, 1L );

        // Perform the request
        CURLcode res = curl_easy_perform( curlHandle );

        // Clean up
        curl_easy_cleanup( curlHandle );
        outfile.close();

        if( res != CURLE_OK )
        {
            std::cerr << "Download failed with error: "
                << curl_easy_strerror( res ) << std::endl;
            return false;
        }

        // Optionally check the HTTP response code
        long http_code = 0;
        curl_easy_getinfo( curlHandle, CURLINFO_RESPONSE_CODE, &http_code );
        if( http_code != 200 )
        {
            std::cerr << "Non-200 status code received: " << http_code << std::endl;
            return false;
        }

        std::cout << "File downloaded successfully: " << outPath.FullPath << std::endl;
        return true;
    }
#endif
}
