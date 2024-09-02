#include "PCH.h"
#include "DebugTools.h"

#if USING( ME_GAME_TOOLS )

#include "imgui.h"
#include "Window/SDLWindow.h"
#include "Window/PlatformWindowHooks.h"
#include "Engine/Engine.h"
#include "Renderer.h"

static SDL_Cursor* g_imgui_to_sdl_cursor[ImGuiMouseCursor_COUNT];

DebugTools::DebugTools()
{

}


DebugTools::~DebugTools()
{

}

void DebugTools::Init()
{
    auto registry = GetWidgetRegistry();

    for( auto it : registry )
    {
        auto customWidget = it.second.CreateFunc();
        CustomRegisteredWidgets.push_back( customWidget );
        customWidget->Init();
    }
    ImGuiIO& io = ImGui::GetIO(); (void)io;
    io.ConfigFlags |= ImGuiConfigFlags_ViewportsEnable;
    io.BackendFlags |= ImGuiBackendFlags_PlatformHasViewports;
    io.BackendFlags |= ImGuiBackendFlags_RendererHasViewports;

    g_imgui_to_sdl_cursor[ImGuiMouseCursor_Arrow] = SDL_CreateSystemCursor( SDL_SYSTEM_CURSOR_ARROW );
    g_imgui_to_sdl_cursor[ImGuiMouseCursor_TextInput] = SDL_CreateSystemCursor( SDL_SYSTEM_CURSOR_IBEAM );
    g_imgui_to_sdl_cursor[ImGuiMouseCursor_Hand] = SDL_CreateSystemCursor( SDL_SYSTEM_CURSOR_HAND );
    g_imgui_to_sdl_cursor[ImGuiMouseCursor_ResizeNS] = SDL_CreateSystemCursor( SDL_SYSTEM_CURSOR_SIZENS );
    g_imgui_to_sdl_cursor[ImGuiMouseCursor_ResizeEW] = SDL_CreateSystemCursor( SDL_SYSTEM_CURSOR_SIZEWE );
    g_imgui_to_sdl_cursor[ImGuiMouseCursor_ResizeNESW] = SDL_CreateSystemCursor( SDL_SYSTEM_CURSOR_SIZENESW );
    g_imgui_to_sdl_cursor[ImGuiMouseCursor_ResizeNWSE] = SDL_CreateSystemCursor( SDL_SYSTEM_CURSOR_SIZENWSE );

    ImGui::InitHooks( (SDLWindow*)GetEngine().GetWindow(), GetEngine().GetRenderer().GetImGuiRenderer() );
}

void DebugTools::Render()
{
    ImGui::Begin( "Debug Tools" );

    if( ImGui::BeginMenu( "View" ) )
    {
        {
            for( auto& i : CustomRegisteredWidgets )
            {
                if( ImGui::MenuItem( i->Name.c_str(), i->Hotkey.c_str(), &i->IsOpen ) )
                {
                }
            }
        }
        ImGui::EndMenu();
    }
    ImGui::End();
    // Custom User Widgets
    {
        OPTICK_CATEGORY( "Custom User Widgets", Optick::Category::UI );
        for( auto customWidget : CustomRegisteredWidgets )
        {
            if( customWidget->IsOpen )
            {
                customWidget->Render();
            }
        }
    }
}

#endif