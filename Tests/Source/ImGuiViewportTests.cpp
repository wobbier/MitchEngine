#include <doctest/doctest.h>
#include <imgui.h>
#include <imgui_internal.h>

namespace
{
    // Exercise ImGui's real viewport selection and lifecycle without an OS window or GPU.
    struct ViewportFixture
    {
        ImGuiContext* Previous = ImGui::GetCurrentContext();
        ImGuiContext* Context = ImGui::CreateContext();
        int Created = 0;
        int Destroyed = 0;

        ViewportFixture()
        {
            ImGui::SetCurrentContext( Context );
            ImGuiIO& io = ImGui::GetIO();
            io.IniFilename = nullptr;
            io.DisplaySize = ImVec2( 640, 480 );
            io.ConfigFlags |= ImGuiConfigFlags_ViewportsEnable;
            io.BackendFlags |= ImGuiBackendFlags_PlatformHasViewports | ImGuiBackendFlags_RendererHasViewports;
            io.UserData = this;
            unsigned char* pixels;
            int width, height;
            io.Fonts->GetTexDataAsRGBA32( &pixels, &width, &height );

            auto& platform = ImGui::GetPlatformIO();
            platform.Platform_CreateWindow = []( ImGuiViewport* viewport ) {
                ++static_cast<ViewportFixture*>( ImGui::GetIO().UserData )->Created;
                viewport->PlatformHandle = viewport;
                viewport->PlatformUserData = viewport;
            };
            platform.Platform_DestroyWindow = []( ImGuiViewport* viewport ) {
                if( viewport->PlatformUserData )
                    ++static_cast<ViewportFixture*>( ImGui::GetIO().UserData )->Destroyed;
                viewport->PlatformHandle = viewport->PlatformUserData = nullptr;
            };
            platform.Platform_ShowWindow = []( ImGuiViewport* ) {};
            platform.Platform_GetWindowPos = []( ImGuiViewport* viewport ) { return viewport->Pos; };
            platform.Platform_SetWindowPos = []( ImGuiViewport* viewport, ImVec2 pos ) { viewport->Pos = pos; };
            platform.Platform_GetWindowSize = []( ImGuiViewport* viewport ) { return viewport->Size; };
            platform.Platform_SetWindowSize = []( ImGuiViewport* viewport, ImVec2 size ) { viewport->Size = size; };
            platform.Platform_SetWindowTitle = []( ImGuiViewport*, const char* ) {};
            ImGuiPlatformMonitor monitor;
            monitor.MainSize = monitor.WorkSize = ImVec2( 1920, 1080 );
            platform.Monitors.push_back( monitor );
            ImGui::GetMainViewport()->PlatformHandle = ImGui::GetMainViewport();
        }

        ~ViewportFixture()
        {
            ImGui::DestroyContext( Context );
            ImGui::SetCurrentContext( Previous );
        }

        ImGuiViewport* BeginHost( bool detached )
        {
            ImGui::NewFrame();
            ImGui::SetNextWindowPos( detached ? ImVec2( 900, 200 ) : ImVec2( 20, 20 ) );
            ImGui::SetNextWindowSize( ImVec2( 320, 240 ) );
            ImGui::Begin( "Viewport test panel" );
            return ImGui::GetWindowViewport();
        }

        void EndHost()
        {
            ImGui::End();
            ImGui::Render();
            ImGui::UpdatePlatformWindows();
        }
    };
}

TEST_CASE( "ImGui: edge tooltips stay in the main or detached host window" )
{
    bool detached = false;
    SUBCASE( "main window" ) {}
    SUBCASE( "detached window" ) { detached = true; }

    ViewportFixture fixture;
    for( int frame = 0; frame < 5; ++frame )
    {
        ImGuiViewport* host = fixture.BeginHost( detached );
        ImGui::GetIO().MousePos = ImVec2( host->Pos.x + host->Size.x - 1, host->Pos.y + host->Size.y - 1 );
        REQUIRE( ImGui::BeginTooltip() );
        ImGui::TextUnformatted( "Tooltip at the bottom right edge of its host window" );
        CHECK( ImGui::GetWindowViewport() == host );
        CHECK_FALSE( ImGui::GetCurrentWindow()->ViewportOwned );
        CHECK( ImGui::GetCurrentWindow()->ViewportAllowPlatformMonitorExtend == -1 );
        ImGui::EndTooltip();
        ImGui::SetTooltip( "SetTooltip uses the same host" );
        CHECK( ImGui::FindWindowByName( "##Tooltip_01" )->Viewport == host );
        fixture.EndHost();
    }
    CHECK( fixture.Created == ( detached ? 1 : 0 ) );
}

TEST_CASE( "ImGui: drag previews cannot create native tooltip windows" )
{
    ViewportFixture fixture;
    for( int frame = 0; frame < 5; ++frame )
    {
        ImGuiViewport* host = fixture.BeginHost( true );
        // The pointer has left every OS window while the source panel still owns the drag.
        ImGui::GetIO().MousePos = ImVec2( 1600, 900 );
        fixture.Context->DragDropWithinSource = true;
        REQUIRE( ImGui::BeginTooltip() );
        ImGui::TextUnformatted( "Dragged asset preview" );
        CHECK( ImGui::GetWindowViewport() == host );
        CHECK_FALSE( ImGui::GetCurrentWindow()->ViewportOwned );
        ImGui::EndTooltip();
        fixture.Context->DragDropWithinSource = false;
        fixture.EndHost();
    }
    CHECK( fixture.Created == 1 );
}

TEST_CASE( "ImGui: detached panels can merge back into the main viewport" )
{
    ViewportFixture fixture;
    for( int frame = 0; frame < 4; ++frame )
    {
        CHECK( fixture.BeginHost( true ) != ImGui::GetMainViewport() );
        fixture.EndHost();
    }
    CHECK( fixture.Created == 1 );
    for( int frame = 0; frame < 4; ++frame )
    {
        fixture.BeginHost( false );
        fixture.EndHost();
    }
    CHECK( ImGui::FindWindowByName( "Viewport test panel" )->Viewport == ImGui::GetMainViewport() );
    CHECK( fixture.Destroyed == 1 );
}
