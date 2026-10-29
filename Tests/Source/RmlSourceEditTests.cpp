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


TEST_CASE( "UI designer write-back: elements are inserted and removed on their own lines" )
{
    const std::string source =
        "<rml>\n"
        "<body>\n"
        "\t<div id=\"panel\">\n"
        "\t\t<h1 id=\"title\">Hi</h1>\n"
        "\t\t<div id=\"inner\"><div>nested</div></div>\n"
        "\t</div>\n"
        "\t<div id=\"empty\"></div>\n"
        "\t<img id=\"logo\" src=\"a.png\"/>\n"
        "</body>\n"
        "</rml>\n";
    std::string out;

    // The last child, at the children's indentation.
    REQUIRE( RmlSourceEdit::InsertChild( source, "panel", "<button id=\"go\">Go</button>", out ) );
    CHECK( out.find( "\t\t<div id=\"inner\"><div>nested</div></div>\n\t\t<button id=\"go\">Go</button>\n\t</div>\n" ) != std::string::npos );
    // An empty one-line parent opens up.
    REQUIRE( RmlSourceEdit::InsertChild( source, "empty", "<p id=\"p\">P</p>", out ) );
    CHECK( out.find( "\t<div id=\"empty\">\n\t\t<p id=\"p\">P</p>\n\t</div>\n" ) != std::string::npos );
    CHECK_FALSE( RmlSourceEdit::InsertChild( source, "logo", "<p/>", out ) );
    CHECK_FALSE( RmlSourceEdit::InsertChild( source, "missing", "<p/>", out ) );

    // Removal takes the element's line; nested same-name tags are matched.
    REQUIRE( RmlSourceEdit::RemoveElement( source, "inner", out ) );
    CHECK( out.find( "\t\t<h1 id=\"title\">Hi</h1>\n\t</div>\n" ) != std::string::npos );
    REQUIRE( RmlSourceEdit::RemoveElement( source, "panel", out ) );
    CHECK( out == "<rml>\n<body>\n\t<div id=\"empty\"></div>\n\t<img id=\"logo\" src=\"a.png\"/>\n</body>\n</rml>\n" );
    REQUIRE( RmlSourceEdit::RemoveElement( source, "logo", out ) );
    CHECK( out.find( "logo" ) == std::string::npos );

    CHECK( RmlSourceEdit::UniqueId( source, "button" ) == "button" );
    CHECK( RmlSourceEdit::UniqueId( source, "title" ) == "title2" );

    const std::string head = "<head>\n<!-- <link type=\"text/rcss\" href=\"Old.rcss\"/> -->\n<link type=\"text/rcss\" href=\"A.rcss\"/>\n<link href='B.rcss' type='text/rcss'/>\n</head>";
    CHECK( RmlSourceEdit::GetStyleSheetLinks( head ) == std::vector<std::string>{ "A.rcss", "B.rcss" } );
}


TEST_CASE( "UI designer write-back: RCSS rules change in place, the rest of the sheet doesn't" )
{
    const std::string sheet =
        "/* Header comment: h1 { color: red; } */\n"
        "body {\n"
        "\tfont-size: 18dp;\n"
        "}\n"
        "\n"
        "#title,  h2 {\n"
        "\tcolor: #C9A7FF;\n"
        "\tmargin-bottom: 8dp\n"
        "}\n"
        "@media (max-width: 800px) { h1 { color: blue; } }\n"
        "h1 { display: block; }\n";
    std::string out;

    // Replace a value where it's written (the commented and @media rules are skipped).
    REQUIRE( RmlSourceEdit::SetRuleProperty( sheet, "#title, h2", "color", "#ff3366", out ) );
    CHECK( out == std::string( sheet ).replace( sheet.find( "#C9A7FF" ), 7, "#ff3366" ) );
    CHECK( RmlSourceEdit::GetRuleProperty( out, "#title,h2", "color" ).empty() );   // selectors compare as written
    CHECK( RmlSourceEdit::GetRuleProperty( out, "#title,   h2", "color" ) == "#ff3366" );
    CHECK( RmlSourceEdit::GetRuleProperty( sheet, "h1", "display" ) == "block" );
    CHECK( RmlSourceEdit::GetRuleProperty( sheet, "h1", "color" ).empty() );

    // A new declaration at the block's end, after giving the last one its ';'.
    REQUIRE( RmlSourceEdit::SetRuleProperty( sheet, "#title,  h2", "padding", "4dp", out ) );
    CHECK( out.find( "\tcolor: #C9A7FF;\n\tmargin-bottom: 8dp;\n\tpadding: 4dp;\n}\n" ) != std::string::npos );
    // One-line rules stay on one line.
    REQUIRE( RmlSourceEdit::SetRuleProperty( sheet, "h1", "color", "red", out ) );
    CHECK( out.find( "h1 { display: block; color: red; }\n" ) != std::string::npos );

    // Removal takes the declaration's line.
    REQUIRE( RmlSourceEdit::SetRuleProperty( sheet, "body", "font-size", "", out ) );
    CHECK( out.find( "body {\n}\n" ) != std::string::npos );
    REQUIRE( RmlSourceEdit::SetRuleProperty( sheet, "h1", "display", "", out ) );
    CHECK( out.find( "h1 { }\n" ) != std::string::npos );

    // A missing rule is appended.
    REQUIRE( RmlSourceEdit::SetRuleProperty( sheet, "#hint", "color", "white", out ) );
    CHECK( out == sheet + "\n#hint {\n\tcolor: white;\n}\n" );
    CHECK( RmlSourceEdit::HasRule( out, "#hint" ) );
    CHECK_FALSE( RmlSourceEdit::HasRule( sheet, "#hint" ) );
    REQUIRE( RmlSourceEdit::SetRuleProperty( sheet, "#hint", "color", "", out ) );
    CHECK( out == sheet );
}
