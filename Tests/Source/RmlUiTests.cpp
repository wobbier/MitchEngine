#include <doctest/doctest.h>
#include "Dementia.h"

#if USING( ME_RMLUI )

#include "UI/Rml/RmlInterfaces.h"
#include "UI/Rml/RmlRenderInterface.h"
#include "UI/Rml/RmlScriptModels.h"
#include "JSON.h"
#include <RmlUi/Core.h>
#include <bgfx/bgfx.h>
#include <algorithm>
#include <filesystem>
#include <fstream>

namespace RmlUiTest
{
    // Rml on bgfx's Noop renderer (no window, no GPU) with Inter loaded, and one 960x540 context.
    struct NoopRml
    {
        NoopRml()
        {
            bgfx::renderFrame();
            bgfx::Init init;
            init.type = bgfx::RendererType::Noop;
            REQUIRE( bgfx::init( init ) );
            render.Init( false );
            Rml::SetSystemInterface( &system );
            Rml::SetFileInterface( &files );
            Rml::SetRenderInterface( &render );
            REQUIRE( Rml::Initialise() );
            REQUIRE( Rml::LoadFontFace( "Assets/Fonts/Inter-Regular.ttf", true ) );
            Context = Rml::CreateContext( "models", Rml::Vector2i( 960, 540 ) );
            REQUIRE( Context != nullptr );
        }

        ~NoopRml()
        {
            Rml::RemoveContext( "models" );
            Rml::Shutdown();
            render.Shutdown();
            bgfx::shutdown();
        }

        void Frame()
        {
            system.AdvanceTime( 1.0 / 60.0 );
            Context->Update();
            render.BeginFrame( 0, 960, 540 );
            Context->Render();
        }

        RmlSystemInterface system;
        RmlFileInterface files;
        RmlRenderInterface render;
        Rml::Context* Context = nullptr;
    };

    void WriteTextFile( const std::string& InPath, const char* InText )
    {
        std::filesystem::create_directories( std::filesystem::path( InPath ).parent_path() );
        std::ofstream stream( InPath );
        stream << InText;
    }
}

TEST_CASE( "RmlUi: a document lifecycle leaks no geometry or texture handles (bgfx Noop)" )
{
    // Single-threaded Noop renderer: no window, no GPU, handle bookkeeping only.
    bgfx::renderFrame();
    bgfx::Init init;
    init.type = bgfx::RendererType::Noop;
    REQUIRE( bgfx::init( init ) );

    {
        RmlSystemInterface system;
        RmlFileInterface files;
        RmlRenderInterface render;
        // No shader program on Noop; geometry and texture lifecycles still run for real.
        CHECK( render.Init( false ) );
        Rml::SetSystemInterface( &system );
        Rml::SetFileInterface( &files );
        Rml::SetRenderInterface( &render );
        REQUIRE( Rml::Initialise() );
        REQUIRE( Rml::LoadFontFace( "Assets/Fonts/Inter-Regular.ttf", true ) );

        // Text (font atlas -> GenerateTexture), a rounded clip (stencil mask geometry) and a
        // transform: the same features UITest.lvl shows.
        RmlUiTest::WriteTextFile( ".tmp/RmlUiTests/doc.rcss",
            "body { font-family: Inter; font-size: 20px; width: 100%; height: 100%; color: #fff; }\n"
            "#clip { position: absolute; top: 40px; left: 40px; width: 200px; height: 120px;\n"
            "        border-radius: 30px; overflow: hidden; background-color: #123; }\n"
            "#fill { position: absolute; top: -20px; left: -20px; width: 300px; height: 90px;\n"
            "        background-color: #2EC4B6; transform: rotate(-10deg); }\n" );
        RmlUiTest::WriteTextFile( ".tmp/RmlUiTests/doc.rml",
            "<rml><head><link type=\"text/rcss\" href=\"doc.rcss\"/><title>t</title></head>\n"
            "<body><p>leak probe</p><div id=\"clip\"><div id=\"fill\"/></div></body></rml>\n" );

        Rml::Context* context = Rml::CreateContext( "tests", Rml::Vector2i( 960, 540 ) );
        REQUIRE( context != nullptr );
        Rml::ElementDocument* document = context->LoadDocument( ".tmp/RmlUiTests/doc.rml" );
        REQUIRE( document != nullptr );
        document->Show();

        for( int frame = 0; frame < 3; ++frame )
        {
            system.AdvanceTime( 1.0 / 60.0 );
            context->Update();
            render.BeginFrame( 0, 960, 540 );
            context->Render();
        }
        // The render really compiled geometry and the font atlas really generated a texture.
        CHECK( render.GetLiveGeometryCount() > 0 );
        CHECK( render.GetLiveTextureCount() > 0 );

        document->Close();
        context->Update();
        Rml::RemoveContext( "tests" );
        Rml::Shutdown();

        // Everything RmlUi compiled or generated must have been released back through us.
        CHECK( render.GetLiveGeometryCount() == 0 );
        CHECK( render.GetLiveTextureCount() == 0 );

        render.Shutdown();
        std::error_code ec;
        std::filesystem::remove_all( ".tmp/RmlUiTests", ec );
    }

    bgfx::shutdown();
}


TEST_CASE( "RmlUi: script models bind values, and data-event-click arguments reach the script as JSON (ME-312)" )
{
    RmlUiTest::NoopRml rml;
    RmlScriptModels models;
    models.Ensure( "menu" );
    models.SetString( "menu", "label", "Go" );
    models.RegisterEvent( "menu", "pick" );
    models.RegisterEvent( "menu", "close" );
    CHECK( models.Sync( *rml.Context ) );          // new model: documents must (re)load
    CHECK_FALSE( models.Sync( *rml.Context ) );    // nothing new since

    Rml::ElementDocument* document = rml.Context->LoadDocumentFromMemory(
        "<rml><head><style>body { font-family: Inter; }</style></head><body data-model=\"menu\">"
        "<button id=\"two\" data-event-click=\"pick(2, 'fast', true)\">{{ label }}</button>"
        "<button id=\"close\" data-event-click=\"close\">x</button></body></rml>" );
    REQUIRE( document != nullptr );
    document->Show();
    rml.Frame();
    Rml::Element* two = document->GetElementById( "two" );
    REQUIRE( two != nullptr );
    CHECK( two->GetInnerRML() == "Go" );

    // A changed value redraws; an unchanged one isn't dirtied.
    models.SetString( "menu", "label", "Went" );
    CHECK_FALSE( models.Sync( *rml.Context ) );
    rml.Frame();
    CHECK( two->GetInnerRML() == "Went" );

    two->Click();
    document->GetElementById( "close" )->Click();
    CHECK( models.PollEvent( "menu" ) == "pick" );
    const json args = json::parse( models.LastEventArgs( "menu" ) );
    REQUIRE( args.is_array() );
    REQUIRE( args.size() == 3 );
    CHECK( args[0].get<double>() == doctest::Approx( 2.0 ) );
    CHECK( args[1] == "fast" );
    CHECK( args[2] == true );
    CHECK( models.PollEvent( "menu" ) == "close" );
    CHECK( models.LastEventArgs( "menu" ) == "[]" );
    CHECK( models.PollEvent( "menu" ).empty() );
    CHECK( models.PollEvent( "nobody" ).empty() );
    document->Close();
    rml.Frame();
}


TEST_CASE( "RmlUi: a script list drives data-for rows that grow, change and shrink; a row's click names its index (ME-312)" )
{
    RmlUiTest::NoopRml rml;
    RmlScriptModels models;
    models.Ensure( "board" );
    models.SetItemString( "board", "players", 0, "name", "Ada" );
    models.SetItemNumber( "board", "players", 0, "kills", 3 );
    models.SetItemString( "board", "players", 1, "name", "Bo" );   // grows to 2; Bo has no kills yet
    models.RegisterEvent( "board", "pick" );
    CHECK( models.GetListCount( "board", "players" ) == 2 );
    CHECK( models.Sync( *rml.Context ) );

    Rml::ElementDocument* document = rml.Context->LoadDocumentFromMemory(
        "<rml><head><style>body { font-family: Inter; }</style></head><body data-model=\"board\">"
        "<p id=\"count\">{{ players.size }}</p>"
        "<div class=\"row\" data-for=\"row : players\" data-event-click=\"pick(it_index)\">{{ row.name }}:{{ row.kills }}</div>"
        "</body></rml>" );
    REQUIRE( document != nullptr );
    document->Show();
    rml.Frame();

    // The rows data-for made (it keeps its hidden template element, the one with the attribute).
    const auto rowElements = [&]() {
        Rml::ElementList elements;
        document->GetElementsByClassName( elements, "row" );
        elements.erase( std::remove_if( elements.begin(), elements.end(), []( Rml::Element* InElement ) { return InElement->HasAttribute( "data-for" ); } ), elements.end() );
        return elements;
    };
    const auto rows = [&]() {
        std::vector<std::string> texts;
        for( Rml::Element* element : rowElements() )
        {
            texts.push_back( element->GetInnerRML() );
        }
        return texts;
    };
    CHECK( rows() == std::vector<std::string>{ "Ada:3", "Bo:" } );   // an unset field reads ""
    CHECK( document->GetElementById( "count" )->GetInnerRML() == "2" );

    // Changed fields and a new row redraw.
    models.SetItemNumber( "board", "players", 1, "kills", 5 );
    models.SetItemString( "board", "players", 2, "name", "Cy" );
    models.SetItemNumber( "board", "players", 2, "kills", 0 );
    CHECK_FALSE( models.Sync( *rml.Context ) );
    rml.Frame();
    CHECK( rows() == std::vector<std::string>{ "Ada:3", "Bo:5", "Cy:0" } );
    CHECK( document->GetElementById( "count" )->GetInnerRML() == "3" );

    // The third row's click carries its index.
    const Rml::ElementList elements = rowElements();
    REQUIRE( elements.size() == 3 );
    elements[2]->Click();
    CHECK( models.PollEvent( "board" ) == "pick" );
    CHECK( json::parse( models.LastEventArgs( "board" ) ) == json::array( { 2 } ) );

    // Shrinking drops rows from the end.
    models.SetListCount( "board", "players", 1 );
    models.Sync( *rml.Context );
    rml.Frame();
    CHECK( rows() == std::vector<std::string>{ "Ada:3" } );
    CHECK( models.GetListCount( "board", "players" ) == 1 );
    CHECK( models.GetListCount( "board", "nothing" ) == 0 );
    document->Close();
    rml.Frame();
}

#endif
