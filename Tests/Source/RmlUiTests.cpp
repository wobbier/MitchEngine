#include <doctest/doctest.h>
#include "Dementia.h"

#if USING( ME_RMLUI )

#include "UI/Rml/RmlInterfaces.h"
#include "UI/Rml/RmlRenderInterface.h"
#include <RmlUi/Core.h>
#include <bgfx/bgfx.h>
#include <filesystem>
#include <fstream>

namespace RmlUiTest
{
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

#endif
