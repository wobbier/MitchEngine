#include <doctest/doctest.h>
#include "Graphics/ColorLut.h"
#include "Path.h"
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

namespace
{
    bool Parse( const std::string& InText, int& OutSize, std::vector<float>& OutTexels, std::string& OutError, bool& OutDomain )
    {
        OutSize = 0;
        OutTexels.clear();
        OutError.clear();
        OutDomain = false;
        return Moonlight::ColorLut::Parse( InText, OutSize, OutTexels, OutError, OutDomain );
    }
}

TEST_CASE( "ColorLut: parses sizes, comments and colour rows, rejecting malformed files" )
{
    int size = 0;
    std::vector<float> texels;
    std::string error;
    bool domain = false;

    const std::string good =
        "# a comment\r\n"
        "TITLE \"Test\"\n"
        "LUT_3D_SIZE 2\n"
        "DOMAIN_MIN 0.0 0.0 0.0\n"
        "DOMAIN_MAX 1.0 1.0 1.0\n"
        "\n"
        "0 0 0\n1 0 0\n0 1 0\n1 1 0\n0 0 1\n1 0 1\n0 1 1\n1 1 1\n";
    REQUIRE( Parse( good, size, texels, error, domain ) );
    CHECK( size == 2 );
    CHECK( texels.size() == 8 * 4 );
    CHECK_FALSE( domain );
    // Rows run red-fastest; this file is the identity cube.
    CHECK( texels[1 * 4 + 0] == 1.f );
    CHECK( texels[2 * 4 + 1] == 1.f );
    CHECK( texels[4 * 4 + 2] == 1.f );
    CHECK( texels[7 * 4 + 0] == 1.f );
    CHECK( texels[7 * 4 + 1] == 1.f );
    CHECK( texels[7 * 4 + 2] == 1.f );
    CHECK( texels[3] == 1.f ); // alpha pads to one

    CHECK_FALSE( Parse( "LUT_1D_SIZE 2\n0 0\n1 1\n", size, texels, error, domain ) );
    CHECK( error.find( "1D" ) != std::string::npos );
    CHECK_FALSE( Parse( "0 0 0\n", size, texels, error, domain ) );
    CHECK_FALSE( Parse( "LUT_3D_SIZE 2\n0 0 0\n", size, texels, error, domain ) );
    CHECK( error.find( "expected 8" ) != std::string::npos );
    std::string tooMany = "LUT_3D_SIZE 2\n";
    for( int i = 0; i < 9; ++i ) tooMany += "0 0 0\n";
    CHECK_FALSE( Parse( tooMany, size, texels, error, domain ) );
    CHECK_FALSE( Parse( "LUT_3D_SIZE 1\n0 0 0\n", size, texels, error, domain ) );
    CHECK_FALSE( Parse( "LUT_3D_SIZE 2\nWHAT 1 2\n", size, texels, error, domain ) );
    CHECK_FALSE( Parse( "", size, texels, error, domain ) );

    // Non-identity domains parse, flagged so the load can warn (sampling treats them as 0..1).
    const std::string scaled =
        "LUT_3D_SIZE 2\nDOMAIN_MAX 2.0 2.0 2.0\n"
        "0 0 0\n1 0 0\n0 1 0\n1 1 0\n0 0 1\n1 0 1\n0 1 1\n1 1 1\n";
    REQUIRE( Parse( scaled, size, texels, error, domain ) );
    CHECK( domain );
}

TEST_CASE( "ColorLut: the stock engine grades parse and Neutral is an identity" )
{
    const char* names[] = { "Neutral", "TealOrange", "Noir", "Warm", "Cool", "Vintage" };
    for( const char* name : names )
    {
        const Path path( std::string( "Assets/Textures/LUTs/" ) + name + ".cube" );
        if( !path.Exists )
        {
            // Engine content is absent in bare template projects; nothing to check there.
            continue;
        }
        std::ifstream file( path.FullPath, std::ios::binary );
        std::stringstream buffer;
        buffer << file.rdbuf();
        int size = 0;
        std::vector<float> texels;
        std::string error;
        bool domain = false;
        REQUIRE_MESSAGE( Parse( buffer.str(), size, texels, error, domain ), name << ": " << error );
        CHECK( size == 17 );
        CHECK_FALSE( domain );
        if( std::string( name ) == "Neutral" )
        {
            // Texel (r, g, b) holds its own normalized coordinate.
            const int n = size;
            for( int corner = 0; corner < 8; ++corner )
            {
                const int r = ( corner & 1 ) ? n - 1 : 0;
                const int g = ( corner & 2 ) ? n - 1 : 0;
                const int b = ( corner & 4 ) ? n - 1 : 0;
                const size_t index = ( static_cast<size_t>( b ) * n * n + static_cast<size_t>( g ) * n + r ) * 4;
                CHECK( texels[index + 0] == doctest::Approx( r / float( n - 1 ) ) );
                CHECK( texels[index + 1] == doctest::Approx( g / float( n - 1 ) ) );
                CHECK( texels[index + 2] == doctest::Approx( b / float( n - 1 ) ) );
            }
        }
    }
}
