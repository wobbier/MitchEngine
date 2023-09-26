#include "MitchHub.h"

#include <imgui.h>
#include <Engine/Input.h>
#include <Path.h>
#include <Graphics/Texture.h>
#include <Resource/ResourceCache.h>
#include "Utils/ImGuiUtils.h"
#include <utility>
#include <UI/Colors.h>


#include <Window/SDLWindow.h>
#include <optional>
#include <Mathf.h>
#include <ImGui/ImGuiRenderer.h>
#include <Window/PlatformWindowHooks.h>
#include "Utils/PlatformUtils.h"
#include "Dementia.h"
#include "Utils/HUBUtils.h"
#include "Config.h"

MitchHub::MitchHub( Input* input, SDLWindow* window, ImGuiRenderer* renderer )
    : m_input( input )
    , m_window( window )
    , m_renderer( renderer )
{
    //ImGui Init
    {
        ImGuiIO& io = ImGui::GetIO(); (void)io;
        Path EngineConfigFilePath = Path( ".tmp/hub-imgui.cfg" );
        io.IniFilename = EngineConfigFilePath.FullPath.c_str();
        //io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;
        //io.ConfigFlags |= ImGuiConfigFlags_ViewportsEnable;

        io.BackendFlags |= ImGuiBackendFlags_PlatformHasViewports | ImGuiBackendFlags_RendererHasViewports;

        logo = ResourceCache::GetInstance().Get<Moonlight::Texture>( Path( "Assets/LOGO.png" ) );
        closeIcon = ResourceCache::GetInstance().Get<Moonlight::Texture>( Path( "Assets/Close.png" ) );
        minimizeIcon = ResourceCache::GetInstance().Get<Moonlight::Texture>( Path( "Assets/Minimize.png" ) );
        vsIcon = ResourceCache::GetInstance().Get<Moonlight::Texture>( Path( "Assets/VS.png" ) );
        genIcon = ResourceCache::GetInstance().Get<Moonlight::Texture>( Path( "Assets/GEN.png" ) );

        // TODO: Fix the HUB packaging
        ME_ASSERT_MSG( genIcon || vsIcon || minimizeIcon || closeIcon || logo, "TODO: Missing asset for the HUB, probaly a packaging error" );

        ImGuiPlatformIO& platform_io = ImGui::GetPlatformIO();

        ImVec4* colors = ImGui::GetStyle().Colors;
        colors[ImGuiCol_Text] = COLOR_TEXT;
        colors[ImGuiCol_TextDisabled] = ImVec4( 0.50f, 0.50f, 0.50f, 1.00f );
        colors[ImGuiCol_WindowBg] = COLOR_FOREGROUND;
        colors[ImGuiCol_ChildBg] = ImVec4( 1.00f, 1.00f, 1.00f, 0.00f );
        colors[ImGuiCol_PopupBg] = ImVec4( 0.08f, 0.08f, 0.08f, 0.94f );
        colors[ImGuiCol_Border] = ImVec4( 0.43f, 0.43f, 0.50f, 0.50f );
        colors[ImGuiCol_BorderShadow] = ImVec4( 0.00f, 0.00f, 0.00f, 0.00f );
        colors[ImGuiCol_FrameBg] = COLOR_DROPDOWN;
        colors[ImGuiCol_FrameBgHovered] = ImVec4( 0.40f, 0.40f, 0.40f, 0.40f );
        colors[ImGuiCol_FrameBgActive] = ImVec4( 0.18f, 0.18f, 0.18f, 0.67f );
        colors[ImGuiCol_TitleBg] = COLOR_BACKGROUND_BORDER;
        colors[ImGuiCol_TitleBgActive] = COLOR_BACKGROUND_BORDER;
        colors[ImGuiCol_TitleBgCollapsed] = ImVec4( 0.00f, 0.00f, 0.00f, 0.51f );
        colors[ImGuiCol_MenuBarBg] = COLOR_FOREGROUND;
        colors[ImGuiCol_ScrollbarBg] = COLOR_FOREGROUND;
        colors[ImGuiCol_ScrollbarGrab] = COLOR_WHITE_25;
        colors[ImGuiCol_ScrollbarGrabHovered] = ImVec4( 0.41f, 0.41f, 0.41f, 1.00f );
        colors[ImGuiCol_ScrollbarGrabActive] = ImVec4( 0.51f, 0.51f, 0.51f, 1.00f );
        colors[ImGuiCol_CheckMark] = ImVec4( 0.94f, 0.94f, 0.94f, 1.00f );
        colors[ImGuiCol_SliderGrab] = ImVec4( 0.51f, 0.51f, 0.51f, 1.00f );
        colors[ImGuiCol_SliderGrabActive] = ImVec4( 0.86f, 0.86f, 0.86f, 1.00f );
        colors[ImGuiCol_Button] = { 0.220f, 0.220f, 0.220f, 0.25f };
        colors[ImGuiCol_ButtonHovered] = COLOR_PRIMARY_HOVER;
        colors[ImGuiCol_ButtonActive] = COLOR_PRIMARY_PRESS;
        colors[ImGuiCol_Header] = COLOR_WHITE_25;
        colors[ImGuiCol_HeaderHovered] = COLOR_PRIMARY_HOVER;
        colors[ImGuiCol_HeaderActive] = COLOR_PRIMARY_PRESS;
        colors[ImGuiCol_Separator] = COLOR_BACKGROUND_BORDER;
        colors[ImGuiCol_SeparatorHovered] = COLOR_PRIMARY_HOVER;
        colors[ImGuiCol_SeparatorActive] = COLOR_PRIMARY_PRESS;
        colors[ImGuiCol_ResizeGrip] = ImVec4( 0.91f, 0.91f, 0.91f, 0.25f );
        colors[ImGuiCol_ResizeGripHovered] = ImVec4( 0.81f, 0.81f, 0.81f, 0.67f );
        colors[ImGuiCol_ResizeGripActive] = ImVec4( 0.46f, 0.46f, 0.46f, 0.95f );
        colors[ImGuiCol_PlotLines] = ImVec4( 0.61f, 0.61f, 0.61f, 1.00f );
        colors[ImGuiCol_PlotLinesHovered] = ImVec4( 1.00f, 0.43f, 0.35f, 1.00f );
        colors[ImGuiCol_PlotHistogram] = ImVec4( 0.73f, 0.60f, 0.15f, 1.00f );
        colors[ImGuiCol_PlotHistogramHovered] = ImVec4( 1.00f, 0.60f, 0.00f, 1.00f );
        colors[ImGuiCol_TextSelectedBg] = ImVec4( 0.87f, 0.87f, 0.87f, 0.35f );
        //colors[ImGuiCol_ModalWindowDarkening] = ImVec4(0.80f, 0.80f, 0.80f, 0.35f);
        colors[ImGuiCol_DragDropTarget] = ImVec4( 1.00f, 1.00f, 0.00f, 0.90f );
        colors[ImGuiCol_NavHighlight] = ImVec4( 0.60f, 0.60f, 0.60f, 1.00f );
        colors[ImGuiCol_NavWindowingHighlight] = ImVec4( 1.00f, 1.00f, 1.00f, 0.70f );
        colors[ImGuiCol_Tab] = COLOR_FOREGROUND;
        colors[ImGuiCol_TabHovered] = COLOR_PRIMARY_HOVER;
        colors[ImGuiCol_TabActive] = COLOR_PRIMARY;
        colors[ImGuiCol_TabUnfocused] = COLOR_TITLE;
        colors[ImGuiCol_TabUnfocusedActive] = COLOR_FOREGROUND;
        colors[ImGuiCol_TableHeaderBg] = COLOR_HEADER;
        colors[ImGuiCol_TableRowBg] = COLOR_TITLE;
        colors[ImGuiCol_TableRowBgAlt] = COLOR_RECESSED;
    }

    // Loading the project cache
    {
        Cache.Load();
    }

    // Hooking into ImGui
    {
        ImGui::InitHooks( m_window, m_renderer );
    }

    /*{
        ProjectEntry p;
        p.BackgroundImage = ResourceCache::GetInstance().Get<Moonlight::Texture>(Path("fl.png"));
        p.TitleImage = logo;
        p.Name = "Fruit Loops";
        Cache.Projects.push_back(p);
    }

    {
        ProjectEntry p;
        p.BackgroundImage = ResourceCache::GetInstance().Get<Moonlight::Texture>(Path("run.png"));
        p.TitleImage = logo;
        p.Name = "Fruit Loops: Secret Department";
        Cache.Projects.push_back(p);
    }*/

    // Setup the borderless window
    {
        auto cb = [this]( const Vector2& pos ) -> std::optional<SDL_HitTestResult>
        {
            if ( pos > TitleBarDragPosition && pos < TitleBarDragPosition + TitleBarDragSize )
            {
                return SDL_HitTestResult::SDL_HITTEST_DRAGGABLE;
            }

            return std::nullopt;
        };
        m_window->SetBorderless( true );
        m_window->SetCustomDragCallback( cb );
    }
}

void MitchHub::Draw()
{
#if USING( ME_PLATFORM_WIN64 )
    if ( m_input->WasKeyPressed( KeyCode::A ) )
    {
        HUB::ShowOpenFilePrompt( m_window->GetWindowPtr() );
    }
#endif

    ImGuiViewport* viewport = ImGui::GetMainViewport();
    TitleBarDragSize = { viewport->Size.x - SystemButtonSize - 1.f, 50.f };
    {
        ImGui::SetNextWindowPos( { 0.f, 0.f } );
        ImGui::SetNextWindowSize( viewport->Size );
        ImGui::PushStyleVar( ImGuiStyleVar_FramePadding, { 0.f, 0.f } );
        ImGui::PushStyleVar( ImGuiStyleVar_WindowPadding, { 0.f, 0.f } );
        ImGui::PushStyleVar( ImGuiStyleVar_ItemSpacing, { 0.f, 0.f } );
        ImGui::PushStyleVar( ImGuiStyleVar_ChildBorderSize, 0.f );
        ImGui::Begin( "MAIN", 0, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoTitleBar );
        {
            static float wOffset = 350.0f;
            static bool IsMetaPanelOpen = true;
            float h = ImGui::GetContentRegionAvail().y;
            //ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(0, 0));
            ImGui::BeginChild( "child1", ImVec2( wOffset, h ), true );
            {
                ImGui::SetCursorPos( { 25.f, 25.f } );
                ImGui::Image( logo->TexHandle, { 50.f, 50.f } );
                ImGui::SetCursorPos( ImVec2( 0.f, ImGui::GetCursorPos().y + 25.f ) );

                ImGui::PushStyleVar( ImGuiStyleVar_FramePadding, { 0.f, 15.f } );
                ImGui::PushStyleVar( ImGuiStyleVar_ItemSpacing, { 0.f, 10.f } );
                for ( int i = 0; i < Cache.Projects.size(); ++i )
                {
                    ImGuiTreeNodeFlags node_flags = ImGuiTreeNodeFlags_OpenOnArrow | ImGuiTreeNodeFlags_OpenOnDoubleClick | ImGuiTreeNodeFlags_SpanAvailWidth | ImGuiTreeNodeFlags_FramePadding | ( SelectedProjectIndex == i ? ImGuiTreeNodeFlags_Selected : 0 );
                    node_flags |= ImGuiTreeNodeFlags_Leaf | ImGuiTreeNodeFlags_NoTreePushOnOpen; // ImGuiTreeNodeFlags_Bullet
                    std::string name = Cache.Projects[i].Name;
                    if ( name.empty() )
                    {
                        name = Cache.Projects[i].ProjectPath.FullPath.substr( Cache.Projects[i].ProjectPath.FullPath.rfind( '/' ) + 1, Cache.Projects[i].ProjectPath.FullPath.size() );
                    }
                    if ( ImGui::TreeNodeEx( (void*)(intptr_t)i, node_flags, name.c_str() ) )
                    {
                        if ( ImGui::IsItemClicked() )
                        {
                            SelectedProjectIndex = i;
                        }
                    }
                }
                if ( ImGui::Button( "Add Project", { -1.f, 0.f } ) )
                {
#if USING( ME_PLATFORM_WIN64 )
                    Path path = HUB::ShowOpenFilePrompt( m_window->GetWindowPtr() );
                    if ( path.Exists )
                    {
                        ProjectEntry p;
                        p.ProjectPath = path;
                        Path projectConfig = Path( path.FullPath + "/Project/Game.meproj" );
                        //if( projectConfig.Exists )
                        //{
                        //    Config cfg = Config( projectConfig );
                        //    p.Name = cfg.GetJsonObject( "ProjectName" );
                        //}
                        //else
                        //{
                        //    p.Name = path.LocalPath;
                        //}

                        p.TitleImage = ResourceCache::GetInstance().Get<Moonlight::Texture>( Path( path.FullPath + "/Project/Title.png" ) );
                        p.BackgroundImage = ResourceCache::GetInstance().Get<Moonlight::Texture>( Path( path.FullPath + "/Project/Background.png" ) );

                        Cache.Projects.push_back( p );
                        Cache.Save();
                    }
#endif
                }
                ImGui::PopStyleVar( 2 );
            }
            ImGui::EndChild();

            IsMetaPanelOpen = !Cache.Projects.empty();

            if ( IsMetaPanelOpen )
            {
                ImGui::SameLine();
                ImGui::Button( "##vsplitter", ImVec2( 4.0f, h ) );
                if ( ImGui::IsItemHovered() )
                {
                    ImGui::SetMouseCursor( ImGuiMouseCursor_ResizeEW );
                }
                if ( ImGui::IsItemActive() )
                    wOffset += ImGui::GetIO().MouseDelta.x;
                ImGui::SameLine();


                ImGui::BeginChild( "child2", ImVec2( -1.f, h ), true, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoTitleBar );
                auto& bgTexture = Cache.GetActiveBackgroundTexture( SelectedProjectIndex );
                float windowCursorPos = ImGui::GetCursorPosX();
                float panelWidth = ImGui::GetContentRegionAvail().x;
                float xPos = 0.f;
                float yPos = 0.f;
                if ( bgTexture )
                {
                    bool contentAreaTaller = ImGui::GetContentRegionAvail().y > panelWidth;
                    float height = bgTexture->mHeight;
                    float width = bgTexture->mWidth;

                    const float maxWidth = panelWidth, maxHeight = ImGui::GetContentRegionAvail().y;
                    const float bestRatio = std::max( maxWidth / width, maxHeight / height );
                    width *= bestRatio;
                    height *= bestRatio;

                    if ( panelWidth < width )
                    {
                        xPos = -( ( width - panelWidth ) / 2.f );
                    }

                    if ( ImGui::GetContentRegionAvail().y < height )
                    {
                        yPos = -( ( height - ImGui::GetContentRegionAvail().y ) / 2.f );
                    }

                    ImGui::SetCursorPosX( xPos );
                    ImGui::SetCursorPosY( yPos );
                    ImGui::Image( bgTexture->TexHandle, { width, height } );
                }

                auto& titleTexture = Cache.GetActiveTitleTexture( SelectedProjectIndex );
                if ( titleTexture )
                {
                    Vector2 size = Mathf::KeepAspect( { titleTexture->mWidth, titleTexture->mHeight }, { 400, 200 } );

                    ImGui::SetCursorPos( { 100.f, ImGui::GetWindowHeight() / 8.f } );
                    ImGui::Image( titleTexture->TexHandle, { size.x, size.y } );
                }

                ImGui::SetCursorPos( { ImGui::GetWindowWidth() - SystemButtonSize, 0.f } );
                ImGui::PushStyleColor( ImGuiCol_Button, { 0.f, 0.f, 0.f, 0.f } );
                if ( ImGui::ImageButton( closeIcon->TexHandle, { SystemButtonSize, SystemButtonSize } ) )
                {
                    m_window->Exit();
                }
                ImGui::PopStyleColor();

                ImU32 top = 0x00000000;
                ImU32 bot = 0xff000000;
                ImDrawList* draw_list = ImGui::GetWindowDrawList();
                draw_list->AddRectFilledMultiColor( ImVec2( wOffset, ImGui::GetWindowHeight() - 100.f ), ImVec2( wOffset + panelWidth + 80.f, ImGui::GetWindowHeight() ), top, top, bot, bot );

                //68217A, 104, 33, 122
                ImGui::PushStyleColor( ImGuiCol_Button, { 104.f / 255.f, 33.f / 255.f, 122.f / 255.f, 1.f } );
                ImGui::PushStyleVar( ImGuiStyleVar_FramePadding, { 8.f, 8.f } );
                ImGui::PushStyleVar( ImGuiStyleVar_FrameRounding, 4.f );
                {
                    ImGui::PushID( 1 );
                    ImGui::SetCursorPos( { ImGui::GetWindowWidth() - 275.f, ImGui::GetWindowHeight() - 75.f } );
                    if( ImGui::ImageButton( vsIcon->TexHandle, { 211.f, 36.f } ) )
                    {

                        PlatformUtils::OpenFile( Path( Cache.Projects[SelectedProjectIndex].ProjectPath.FullPath + std::string( "/" + Cache.Projects[SelectedProjectIndex].ProjectConfig.Name + ".sln" ) ) );
                    }
                    ImGui::PopID();
                }
                ImGui::PopStyleColor( 1 );
                // 55AAFF 81 170 255
                ImGui::PushStyleColor( ImGuiCol_Button, { 81.f / 255.f, 170.f / 255.f, 255.f / 255.f, 1.f } );
                ImGui::SetCursorPos( { ImGui::GetWindowWidth() - 475.f, ImGui::GetWindowHeight() - 75.f } );
                if ( genIcon && ImGui::ImageButton( genIcon->TexHandle, { 168.f, 36.f } ) )
                {

                    PlatformUtils::SystemCall( Path( Cache.Projects[SelectedProjectIndex].ProjectPath.FullPath + std::string( "/Project/GenerateSolution.bat" ) ) );
                }

                ImGui::PopStyleColor();
                ImGui::PopStyleVar( 2 );

                ImGui::EndChild();
            }
            else
            {
                //TryDestroyMetaFile();
            }

        }
        ImGui::End();
        ImGui::PopStyleVar( 4 );
    }

    static bool showDemo = true;
    ImGui::ShowDemoWindow( &showDemo );
}

ImGuiRenderer* MitchHub::GetRenderer() const
{
    return m_renderer;
}

