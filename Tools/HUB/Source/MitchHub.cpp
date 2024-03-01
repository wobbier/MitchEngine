#include "MitchHub.h"

#include <imgui.h>
#include <Path.h>
#include <Graphics/Texture.h>
#include <Resource/ResourceCache.h>
#include "Utils/ImGuiUtils.h"
#include <utility>

#include <optional>
#include <Mathf.h>
#include "Utils/PlatformUtils.h"
#include "Dementia.h"
#include "Utils/HUBUtils.h"
#include "Config.h"
#include "Window/SDLWindow.h"


MitchHub::MitchHub( ToolCreationFlags& inToolCreationFlags )
    : Tool( inToolCreationFlags )
{
}


void MitchHub::OnStart()
{
    // Load Icons
    logo = ResourceCache::GetInstance().Get<Moonlight::Texture>( Path( "Assets/LOGO.png" ) );
    closeIcon = ResourceCache::GetInstance().Get<Moonlight::Texture>( Path( "Assets/Close.png" ) );
    minimizeIcon = ResourceCache::GetInstance().Get<Moonlight::Texture>( Path( "Assets/Minimize.png" ) );
    vsIcon = ResourceCache::GetInstance().Get<Moonlight::Texture>( Path( "Assets/VS.png" ) );
    genIcon = ResourceCache::GetInstance().Get<Moonlight::Texture>( Path( "Assets/GEN.png" ) );

    // TODO: Fix the HUB packaging
    ME_ASSERT_MSG( genIcon || vsIcon || minimizeIcon || closeIcon || logo, "TODO: Missing asset for the HUB, probaly a packaging error" );

    // Loading the project cache
    Cache.Load();
}

void MitchHub::OnUpdate()
{
#if USING( ME_PLATFORM_WIN64 )
    if( m_input.WasKeyPressed( KeyCode::A ) )
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
                for( int i = 0; i < Cache.Projects.size(); ++i )
                {
                    ImGuiTreeNodeFlags node_flags = ImGuiTreeNodeFlags_OpenOnArrow | ImGuiTreeNodeFlags_OpenOnDoubleClick | ImGuiTreeNodeFlags_SpanAvailWidth | ImGuiTreeNodeFlags_FramePadding | ( SelectedProjectIndex == i ? ImGuiTreeNodeFlags_Selected : 0 );
                    node_flags |= ImGuiTreeNodeFlags_Leaf | ImGuiTreeNodeFlags_NoTreePushOnOpen; // ImGuiTreeNodeFlags_Bullet
                    std::string name = Cache.Projects[i].Name;
                    if( name.empty() )
                    {
                        name = Cache.Projects[i].ProjectPath.FullPath.substr( Cache.Projects[i].ProjectPath.FullPath.rfind( '/' ) + 1, Cache.Projects[i].ProjectPath.FullPath.size() );
                    }
                    if( ImGui::TreeNodeEx( (void*)(intptr_t)i, node_flags, name.c_str() ) )
                    {
                        if( ImGui::IsItemClicked() )
                        {
                            SelectedProjectIndex = i;
                        }
                    }
                }
                if( ImGui::Button( "Add Project", { -1.f, 0.f } ) )
                {
#if USING( ME_PLATFORM_WIN64 )
                    Path path = HUB::ShowOpenFilePrompt( m_window->GetWindowPtr() );
                    if( path.Exists )
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

            if( IsMetaPanelOpen )
            {
                ImGui::SameLine();
                ImGui::Button( "##vsplitter", ImVec2( 4.0f, h ) );
                if( ImGui::IsItemHovered() )
                {
                    ImGui::SetMouseCursor( ImGuiMouseCursor_ResizeEW );
                }
                if( ImGui::IsItemActive() )
                    wOffset += ImGui::GetIO().MouseDelta.x;
                ImGui::SameLine();


                ImGui::BeginChild( "child2", ImVec2( -1.f, h ), true, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoTitleBar );
                auto bgTexture = Cache.GetActiveBackgroundTexture( SelectedProjectIndex );
                float windowCursorPos = ImGui::GetCursorPosX();
                float panelWidth = ImGui::GetContentRegionAvail().x;
                float xPos = 0.f;
                float yPos = 0.f;
                if( bgTexture )
                {
                    bool contentAreaTaller = ImGui::GetContentRegionAvail().y > panelWidth;
                    float height = bgTexture->mHeight;
                    float width = bgTexture->mWidth;

                    const float maxWidth = panelWidth, maxHeight = ImGui::GetContentRegionAvail().y;
                    const float bestRatio = std::max( maxWidth / width, maxHeight / height );
                    width *= bestRatio;
                    height *= bestRatio;

                    if( panelWidth < width )
                    {
                        xPos = -( ( width - panelWidth ) / 2.f );
                    }

                    if( ImGui::GetContentRegionAvail().y < height )
                    {
                        yPos = -( ( height - ImGui::GetContentRegionAvail().y ) / 2.f );
                    }

                    ImGui::SetCursorPosX( xPos );
                    ImGui::SetCursorPosY( yPos );
                    ImGui::Image( bgTexture->TexHandle, { width, height } );
                }

                auto titleTexture = Cache.GetActiveTitleTexture( SelectedProjectIndex );
                if( titleTexture )
                {
                    Vector2 size = Mathf::KeepAspect( { titleTexture->mWidth, titleTexture->mHeight }, { 400, 200 } );

                    ImGui::SetCursorPos( { 100.f, ImGui::GetWindowHeight() / 8.f } );
                    ImGui::Image( titleTexture->TexHandle, { size.x, size.y } );
                }

                ImGui::SetCursorPos( { ImGui::GetWindowWidth() - SystemButtonSize, 0.f } );
                ImGui::PushStyleColor( ImGuiCol_Button, { 0.f, 0.f, 0.f, 0.f } );
                if( ImGui::ImageButton( closeIcon->TexHandle, { SystemButtonSize, SystemButtonSize } ) )
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
                if( genIcon && ImGui::ImageButton( genIcon->TexHandle, { 168.f, 36.f } ) )
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