#include <doctest/doctest.h>
#include "UI/Rml/RmlSourceEdit.h"

TEST_CASE( "UI designer write-back: inline styles change in place, the rest of the file doesn't" )
{
    const std::string source =
        "<rml>\n"
        "<!-- <h1 id=\"title\">commented out</h1> -->\n"
        "\t<h1 id=\"title\" class=\"big\">Hello</h1>\n"
        "\t<p id=\"body\" style=\"color: red; margin: 4dp\">Text</p>\n"
        "\t<img id=\"logo\" src=\"a.png\"/>\n"
        "</rml>\n";
    std::string out;

    // A new style attribute on the real tag (not the commented one); everything else unchanged.
    REQUIRE( RmlSourceEdit::SetInlineStyle( source, "title", "color", "#ff3366", out ) );
    CHECK( out.find( "\t<h1 id=\"title\" class=\"big\" style=\"color: #ff3366;\">Hello</h1>\n" ) != std::string::npos );
    CHECK( out.find( "<!-- <h1 id=\"title\">commented out</h1> -->" ) != std::string::npos );
    CHECK( out.size() == source.size() + std::string( " style=\"color: #ff3366;\"" ).size() );
    CHECK( RmlSourceEdit::GetInlineStyle( out, "title", "color" ) == "#ff3366" );

    // An existing declaration is replaced where it is; another one is appended.
    REQUIRE( RmlSourceEdit::SetInlineStyle( source, "body", "color", "blue", out ) );
    CHECK( out.find( "style=\"color: blue; margin: 4dp\"" ) != std::string::npos );
    REQUIRE( RmlSourceEdit::SetInlineStyle( source, "body", "font-size", "20dp", out ) );
    CHECK( out.find( "style=\"color: red; margin: 4dp; font-size: 20dp;\"" ) != std::string::npos );
    // An empty value removes it.
    REQUIRE( RmlSourceEdit::SetInlineStyle( source, "body", "color", "", out ) );
    CHECK( RmlSourceEdit::GetInlineStyle( out, "body", "color" ).empty() );
    CHECK( RmlSourceEdit::GetInlineStyle( out, "body", "margin" ) == "4dp" );

    // Self-closing tags keep their slash.
    REQUIRE( RmlSourceEdit::SetInlineStyle( source, "logo", "width", "64dp", out ) );
    CHECK( out.find( "<img id=\"logo\" src=\"a.png\" style=\"width: 64dp;\" />" ) != std::string::npos );

    CHECK_FALSE( RmlSourceEdit::SetInlineStyle( source, "missing", "color", "red", out ) );
}
