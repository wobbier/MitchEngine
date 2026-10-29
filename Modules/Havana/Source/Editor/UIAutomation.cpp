#include "Editor/UIAutomation.h"

#include "Dementia.h"
#include "Engine/Engine.h"
#include "Cores/UI/RmlUiCore.h"
#include "Engine/Input.h"
#include "EditorApp.h"
#include "Havana.h"
#include "Widgets/SceneViewWidget.h"
#include <SDL.h>
#include <cmath>
#include <imgui.h>

#include <cstdlib>
#include <map>
#include <utility>

#if USING( ME_RMLUI )
#include <RmlUi/Core.h>
#endif

namespace
{
    std::string Trimmed( const std::string& InText )
    {
        const size_t first = InText.find_first_not_of( " \t\r\n" );
        if( first == std::string::npos )
        {
            return {};
        }
        const size_t last = InText.find_last_not_of( " \t\r\n" );
        return InText.substr( first, last - first + 1 );
    }
}

namespace UIAutomation
{
    bool Execute( const std::string& InCommand, const std::string& InArgs, std::string& OutError )
    {
#if USING( ME_RMLUI )
        RmlUiCore* core = GetEngine().RmlUi;
        Rml::Context* context = core != nullptr ? core->GetContext() : nullptr;
        if( context == nullptr )
        {
            OutError = "No RmlUi context exists yet";
            return false;
        }

        // The real mouse path (ME-262): move the OS pointer over an element as the Game View
        // shows it, from the image's on-screen rect and the context's size (not the mapping
        // under test), then press and release through the game's mouse buttons.
        //   ui-mouse-move #press-me     ui-mouse-down     ui-mouse-up
        if( InCommand == "ui-mouse-move" )
        {
            const std::string selector = Trimmed( InArgs );
            Rml::Element* element = context->GetRootElement()->QuerySelector( selector );
            EditorApp* app = static_cast<EditorApp*>( GetEngine().GetGame() );
            SceneViewWidget* gameView = app && app->Editor ? app->Editor->GetGameView() : nullptr;
            if( element == nullptr || gameView == nullptr )
            {
                OutError = element == nullptr ? "No element matches '" + selector + "'" : "No Game View";
                return false;
            }
            const Rml::Vector2f centre = element->GetAbsoluteOffset( Rml::BoxArea::Border ) + element->GetBox().GetSize( Rml::BoxArea::Border ) * 0.5f;
            const Rml::Vector2i dimensions = context->GetDimensions();
            const Vector2 min = gameView->GetViewportScreenMin();
            const Vector2 size = gameView->GetViewportScreenSize();
            const float x = min.x + centre.x / std::max( dimensions.x, 1 ) * size.x;
            const float y = min.y + centre.y / std::max( dimensions.y, 1 ) * size.y;
            if( ImGui::GetIO().ConfigFlags & ImGuiConfigFlags_ViewportsEnable )
            {
                SDL_WarpMouseGlobal( static_cast<int>( std::lround( x ) ), static_cast<int>( std::lround( y ) ) );
            }
            else
            {
                const ImGuiViewport* main = ImGui::GetMainViewport();
                SDL_WarpMouseInWindow( SDL_GetMouseFocus() ? SDL_GetMouseFocus() : SDL_GetKeyboardFocus(), static_cast<int>( std::lround( x - main->Pos.x ) ),
                    static_cast<int>( std::lround( y - main->Pos.y ) ) );
            }
            return true;
        }
        if( InCommand == "ui-mouse-down" || InCommand == "ui-mouse-up" )
        {
            GetEngine().GetInput().SetSimulatedMouseButtons( InCommand == "ui-mouse-down" ? SDL_BUTTON_LMASK : 0u );
            return true;
        }

        if( InCommand == "ui-click" )
        {
            const std::string selector = Trimmed( InArgs );
            Rml::Element* element = context->GetRootElement()->QuerySelector( selector );
            if( element == nullptr )
            {
                OutError = "No element matches '" + selector + "'";
                return false;
            }
            element->Focus();
            element->DispatchEvent( Rml::EventId::Click, Rml::Dictionary() );
            return true;
        }

        if( InCommand == "ui-assert-text" )
        {
            const size_t bar = InArgs.find( '|' );
            if( bar == std::string::npos )
            {
                OutError = "Usage: ui-assert-text <selector> | <expected>";
                return false;
            }
            const std::string selector = Trimmed( InArgs.substr( 0, bar ) );
            const std::string expected = Trimmed( InArgs.substr( bar + 1 ) );
            Rml::Element* element = context->GetRootElement()->QuerySelector( selector );
            if( element == nullptr )
            {
                OutError = "No element matches '" + selector + "'";
                return false;
            }
            const std::string actual = Trimmed( element->GetInnerRML() );
            if( actual != expected )
            {
                OutError = "'" + selector + "' reads '" + actual + "', expected '" + expected + "'";
                return false;
            }
            return true;
        }

        if( InCommand == "ui-assert-focused" )
        {
            const std::string selector = Trimmed( InArgs );
            Rml::Element* expected = context->GetRootElement()->QuerySelector( selector );
            Rml::Element* focused = context->GetFocusElement();
            if( expected == nullptr )
            {
                OutError = "No element matches '" + selector + "'";
                return false;
            }
            if( focused != expected )
            {
                OutError = "Focus is on '" + ( focused != nullptr ? focused->GetId() : std::string( "<none>" ) ) + "', expected '" + selector + "'";
                return false;
            }
            return true;
        }

        if( InCommand == "ui-set-language" )
        {
            core->SetLanguage( Trimmed( InArgs ) );
            return true;
        }

        if( InCommand == "ui-set-text" )
        {
            // ui-set-text #click-result | Unclicked   (resets a label between checks)
            const size_t bar = InArgs.find( '|' );
            const std::string selector = Trimmed( InArgs.substr( 0, bar ) );
            Rml::Element* element = bar == std::string::npos ? nullptr : context->GetRootElement()->QuerySelector( selector );
            if( element == nullptr )
            {
                OutError = "ui-set-text: expected <selector> | <text>, and an element matching it";
                return false;
            }
            element->SetInnerRML( Trimmed( InArgs.substr( bar + 1 ) ) );
            return true;
        }

        if( InCommand == "ui-set-text-scale" )
        {
            core->SetTextScale( static_cast<float>( std::atof( InArgs.c_str() ) ) );
            return true;
        }

        if( InCommand == "input-press" )
        {
            // input-press Gamepad/DPadDown | Key/Tab | Key/ShiftTab ... : one UI navigation
            // press, through the same key path the live gamepad mapping feeds.
            static const std::map<std::string, std::pair<int, int>> kPresses = {
                { "Gamepad/DPadUp", { Rml::Input::KI_UP, 0 } },
                { "Gamepad/DPadDown", { Rml::Input::KI_DOWN, 0 } },
                { "Gamepad/DPadLeft", { Rml::Input::KI_LEFT, 0 } },
                { "Gamepad/DPadRight", { Rml::Input::KI_RIGHT, 0 } },
                { "Gamepad/South", { Rml::Input::KI_RETURN, 0 } },
                { "Key/Tab", { Rml::Input::KI_TAB, 0 } },
                { "Key/ShiftTab", { Rml::Input::KI_TAB, Rml::Input::KM_SHIFT } },
                { "Key/Enter", { Rml::Input::KI_RETURN, 0 } },
                { "Key/Up", { Rml::Input::KI_UP, 0 } },
                { "Key/Down", { Rml::Input::KI_DOWN, 0 } },
                { "Key/Left", { Rml::Input::KI_LEFT, 0 } },
                { "Key/Right", { Rml::Input::KI_RIGHT, 0 } },
            };
            const auto press = kPresses.find( Trimmed( InArgs ) );
            if( press == kPresses.end() )
            {
                OutError = "Unknown press '" + Trimmed( InArgs ) + "'";
                return false;
            }
            core->InjectUiKey( press->second.first, press->second.second );
            return true;
        }

        OutError = "Unknown UI command: " + InCommand;
        return false;
#else
        OutError = "RmlUi is not built";
        return false;
#endif
    }
}
