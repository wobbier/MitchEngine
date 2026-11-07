#include "ImGuiAutomation.h"
#if USING( ME_EDITOR )
#include "Widgets/ItemLog.h"
#include "Utils/BGFXCallback.h"
#include "Engine/Engine.h"
#include "Renderer.h"
#include "CLog.h"

#include <SDL.h>
#include <imgui.h>
#include <imgui_internal.h>
#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <sstream>
#include <vector>

namespace ImGuiAutomation
{
    namespace
    {
        using MeWidgets::ItemLog::Item;

        // One frame of input. Positions are absolute (ImGui's desktop coordinates with viewports).
        struct Step
        {
            ImVec2 Pos;
            int Button = 0;
            int Press = 0;              // +1 down, -1 up, 0 none
            std::vector<std::pair<ImGuiKey, bool>> Keys;
            std::string Text;
        };

        struct Gesture
        {
            bool Active = false;
            std::vector<Step> Steps;
            size_t Next = 0;
            float SavedDoubleClickTime = -1.f;
        };

        Gesture& Current()
        {
            static Gesture gesture;
            return gesture;
        }

        // Scrolling an item into view: frames left before giving up.
        int& ScrollFrames()
        {
            static int frames = 0;
            return frames;
        }

        // panel-screenshot: screenshot count before the request, and frames left to wait.
        struct ScreenshotWait
        {
            uint32_t Baseline = 0;
            int Frames = 0;
            std::string Path;
        };
        ScreenshotWait& PanelShot()
        {
            static ScreenshotWait wait;
            return wait;
        }

        std::string Trim( std::string InText )
        {
            const auto first = InText.find_first_not_of( " \t" );
            if( first == std::string::npos ) return {};
            const auto last = InText.find_last_not_of( " \t" );
            return InText.substr( first, last - first + 1 );
        }

        std::vector<std::string> SplitBars( const std::string& InArgs )
        {
            std::vector<std::string> parts;
            size_t start = 0;
            while( true )
            {
                const size_t bar = InArgs.find( '|', start );
                parts.push_back( Trim( InArgs.substr( start, bar == std::string::npos ? std::string::npos : bar - start ) ) );
                if( bar == std::string::npos ) break;
                start = bar + 1;
            }
            return parts;
        }

        std::vector<std::string> Words( const std::string& InText )
        {
            std::vector<std::string> words;
            std::istringstream stream( InText );
            std::string word;
            while( stream >> word ) words.push_back( word );
            return words;
        }

        bool ParseModifiers( const std::vector<std::string>& InWords, std::vector<ImGuiKey>& OutKeys, std::string& OutError )
        {
            for( const std::string& word : InWords )
            {
                if( word == "shift" ) OutKeys.push_back( ImGuiMod_Shift );
                else if( word == "ctrl" ) OutKeys.push_back( ImGuiMod_Ctrl );
                else if( word == "alt" ) OutKeys.push_back( ImGuiMod_Alt );
                else
                {
                    OutError = "unknown modifier '" + word + "' (shift, ctrl, alt)";
                    return false;
                }
            }
            return true;
        }

        void ApplyStep( const Step& InStep )
        {
            ImGuiIO& io = ImGui::GetIO();
            // The SDL backend reads the OS cursor every frame and its position wins over queued
            // events, so the pointer is warped too.
            if( io.ConfigFlags & ImGuiConfigFlags_ViewportsEnable )
            {
                SDL_WarpMouseGlobal( static_cast<int>( std::lround( InStep.Pos.x ) ), static_cast<int>( std::lround( InStep.Pos.y ) ) );
            }
            else
            {
                // Without viewports ImGui's coordinates are relative to the main window.
                const ImGuiViewport* main = ImGui::GetMainViewport();
                SDL_Window* window = static_cast<SDL_Window*>( main->PlatformHandle );
                if( !window ) window = SDL_GetMouseFocus() ? SDL_GetMouseFocus() : SDL_GetKeyboardFocus();
                SDL_WarpMouseInWindow( window, static_cast<int>( std::lround( InStep.Pos.x - main->Pos.x ) ), static_cast<int>( std::lround( InStep.Pos.y - main->Pos.y ) ) );
            }
            io.AddMousePosEvent( InStep.Pos.x, InStep.Pos.y );
            for( const auto& [key, down] : InStep.Keys )
            {
                io.AddKeyEvent( key, down );
            }
            if( InStep.Press != 0 )
            {
                io.AddMouseButtonEvent( InStep.Button, InStep.Press > 0 );
            }
            if( !InStep.Text.empty() )
            {
                io.AddInputCharactersUTF8( InStep.Text.c_str() );
            }
        }

        // The item's window, for scrolling it into view.
        ImGuiWindow* WindowOf( const Item& InItem )
        {
            ImGuiContext& g = *GImGui;
            for( ImGuiWindow* window : g.Windows )
            {
                if( window->WasActive && window->RootWindow && InItem.Window == window->RootWindow->Name && window->ClipRect.Overlaps( InItem.Rect ) )
                {
                    return window;
                }
            }
            for( ImGuiWindow* window : g.Windows )
            {
                if( window->WasActive && window->RootWindow && InItem.Window == window->RootWindow->Name
                    && InItem.Rect.Min.x >= window->OuterRectClipped.Min.x && InItem.Rect.Max.x <= window->OuterRectClipped.Max.x
                    && window->ScrollMax.y > 0.f )
                {
                    return window;
                }
            }
            return nullptr;
        }

        // Finds the item and makes sure it can be pointed at. OutRetry asks for another frame
        // (the item is being scrolled into view).
        const Item* Resolve( const std::string& InQuery, std::string& OutError, bool& OutRetry )
        {
            const Item* item = MeWidgets::ItemLog::Find( InQuery, OutError );
            if( !item )
            {
                // An item can be missing for a frame while its panel opens or scrolls.
                if( ScrollFrames() == 0 )
                {
                    ScrollFrames() = 30;
                }
                if( --ScrollFrames() > 0 )
                {
                    OutRetry = true;
                }
                return nullptr;
            }
            if( item->IsPointable() )
            {
                ScrollFrames() = 0;
                return item;
            }
            if( ScrollFrames() == 0 )
            {
                ScrollFrames() = 30;
            }
            if( --ScrollFrames() <= 0 )
            {
                OutError = "'" + InQuery + "' stays out of view";
                return nullptr;
            }
            if( ImGuiWindow* window = WindowOf( *item ) )
            {
                ImGui::SetScrollFromPosY( window, item->Rect.GetCenter().y - window->Pos.y, 0.5f );
            }
            OutRetry = true;
            return nullptr;
        }

        void Begin( std::vector<Step> InSteps )
        {
            Gesture& gesture = Current();
            gesture.Active = true;
            gesture.Steps = std::move( InSteps );
            gesture.Next = 0;
        }

        void Hover( std::vector<Step>& OutSteps, ImVec2 InPos )
        {
            // Two frames over the item: one to move, one for ImGui to settle the hover.
            Step step;
            step.Pos = InPos;
            OutSteps.push_back( step );
            OutSteps.push_back( step );
        }

        void Click( std::vector<Step>& OutSteps, ImVec2 InPos, int InButton, const std::vector<ImGuiKey>& InMods )
        {
            Step down;
            down.Pos = InPos;
            down.Button = InButton;
            down.Press = 1;
            for( ImGuiKey key : InMods ) down.Keys.emplace_back( key, true );
            OutSteps.push_back( down );
            Step up;
            up.Pos = InPos;
            up.Button = InButton;
            up.Press = -1;
            OutSteps.push_back( up );
            if( !InMods.empty() )
            {
                Step release;
                release.Pos = InPos;
                for( ImGuiKey key : InMods ) release.Keys.emplace_back( key, false );
                OutSteps.push_back( release );
            }
        }

        void Drag( std::vector<Step>& OutSteps, ImVec2 InFrom, ImVec2 InDelta, const std::vector<ImGuiKey>& InMods )
        {
            Step down;
            down.Pos = InFrom;
            down.Press = 1;
            for( ImGuiKey key : InMods ) down.Keys.emplace_back( key, true );
            OutSteps.push_back( down );
            // Moves of at most 8 px a frame, so ImGui's drag threshold and per-frame deltas behave
            // as they do under a hand.
            const float length = std::max( std::abs( InDelta.x ), std::abs( InDelta.y ) );
            const int moves = std::clamp( static_cast<int>( std::ceil( length / 8.f ) ), 1, 120 );
            for( int i = 1; i <= moves; ++i )
            {
                const float t = static_cast<float>( i ) / static_cast<float>( moves );
                Step move;
                move.Pos = ImVec2( InFrom.x + InDelta.x * t, InFrom.y + InDelta.y * t );
                OutSteps.push_back( move );
            }
            Step hold;
            hold.Pos = ImVec2( InFrom.x + InDelta.x, InFrom.y + InDelta.y );
            OutSteps.push_back( hold );
            Step up = hold;
            up.Press = -1;
            OutSteps.push_back( up );
            Step release = hold;
            for( ImGuiKey key : InMods ) release.Keys.emplace_back( key, false );
            OutSteps.push_back( release );
        }

        bool NearlyEqualText( const std::string& InActual, const std::string& InExpected, float InTolerance )
        {
            char* actualEnd = nullptr;
            char* expectedEnd = nullptr;
            const double actual = std::strtod( InActual.c_str(), &actualEnd );
            const double expected = std::strtod( InExpected.c_str(), &expectedEnd );
            const bool actualNumber = actualEnd != InActual.c_str();
            const bool expectedNumber = expectedEnd != InExpected.c_str() && Trim( expectedEnd ).empty();
            if( actualNumber && expectedNumber )
            {
                return std::abs( actual - expected ) <= InTolerance;
            }
            if( InActual.size() != InExpected.size() ) return false;
            return std::equal( InActual.begin(), InActual.end(), InExpected.begin(), []( char a, char b ) {
                return std::tolower( static_cast<unsigned char>( a ) ) == std::tolower( static_cast<unsigned char>( b ) );
            } );
        }
    }


    bool Handles( const std::string& InCommand )
    {
        return InCommand == "imgui-click" || InCommand == "imgui-drag" || InCommand == "imgui-scrub" || InCommand == "imgui-type"
            || InCommand == "imgui-hover" || InCommand == "imgui-assert-value" || InCommand == "imgui-assert-item"
            || InCommand == "imgui-dump" || InCommand == "panel-screenshot";
    }


    void Tick()
    {
    }


    bool Execute( const std::string& InCommand, const std::string& InArgs, std::string& OutError, bool& OutRetry )
    {
        Gesture& gesture = Current();
        if( gesture.Active )
        {
            // A gesture in progress: one step per frame until it's done.
            ApplyStep( gesture.Steps[gesture.Next++] );
            if( gesture.Next < gesture.Steps.size() )
            {
                OutRetry = true;
            }
            else
            {
                gesture.Active = false;
                if( gesture.SavedDoubleClickTime >= 0.f )
                {
                    ImGui::GetIO().MouseDoubleClickTime = gesture.SavedDoubleClickTime;
                    gesture.SavedDoubleClickTime = -1.f;
                }
                // One more frame so the widget sees the release before the next command.
                OutRetry = false;
            }
            return true;
        }

        const std::vector<std::string> parts = SplitBars( InArgs );
        const std::string query = parts.empty() ? std::string() : parts[0];

        // The item log records only once a flow first asks for it (flows that never drive ImGui
        // pay nothing); it needs one drawn frame before there is anything to find.
        if( InCommand != "panel-screenshot" && !MeWidgets::ItemLog::IsEnabled() )
        {
            MeWidgets::ItemLog::Enable( true );
            OutRetry = true;
            return true;
        }
        if( InCommand != "panel-screenshot" && MeWidgets::ItemLog::LastFrame().empty() )
        {
            OutRetry = true;
            return true;
        }

        if( InCommand == "imgui-dump" )
        {
            CLog::Log( CLog::LogType::Info, "[imgui-dump] " + std::to_string( MeWidgets::ItemLog::LastFrame().size() ) + " items drawn last frame" );
            for( const Item& item : MeWidgets::ItemLog::LastFrame() )
            {
                const std::string line = MeWidgets::ItemLog::Describe( item );
                if( query.empty() || line.find( query ) != std::string::npos )
                {
                    CLog::Log( CLog::LogType::Info, "[imgui-dump] " + line );
                }
            }
            return true;
        }

        if( InCommand == "panel-screenshot" )
        {
            ScreenshotWait& wait = PanelShot();
            if( wait.Frames > 0 )
            {
                if( GetEngine().GetRenderer().GetScreenshotCount() > wait.Baseline )
                {
                    wait.Frames = 0;
                    return true;
                }
                if( --wait.Frames == 0 )
                {
                    Moonlight::BGFXCallback::ClearScreenshotCrop( wait.Path );
                    OutError = "the screenshot never arrived";
                    return false;
                }
                OutRetry = true;
                return true;
            }
            if( parts.size() < 2 || parts[1].empty() )
            {
                OutError = "usage: panel-screenshot <window> | <path.png>";
                return false;
            }
            ImGuiWindow* window = ImGui::FindWindowByName( query.c_str() );
            if( !window || !window->WasActive )
            {
                OutError = "no open window named '" + query + "'";
                return false;
            }
            ImGuiViewport* main = ImGui::GetMainViewport();
            if( window->Viewport && window->Viewport != main )
            {
                OutError = "'" + query + "' floats in its own OS window; dock it to capture it";
                return false;
            }
            // The panel's rect inside the main window, clamped to it, in framebuffer pixels.
            const ImVec2 scale = ImGui::GetIO().DisplayFramebufferScale;
            const float minX = std::max( window->Pos.x - main->Pos.x, 0.f );
            const float minY = std::max( window->Pos.y - main->Pos.y, 0.f );
            const float maxX = std::min( window->Pos.x + window->Size.x - main->Pos.x, main->Size.x );
            const float maxY = std::min( window->Pos.y + window->Size.y - main->Pos.y, main->Size.y );
            if( maxX <= minX || maxY <= minY )
            {
                OutError = "'" + query + "' is outside the main window";
                return false;
            }
            Moonlight::BGFXCallback::SetScreenshotCrop( parts[1], static_cast<uint32_t>( std::lround( minX * scale.x ) ), static_cast<uint32_t>( std::lround( minY * scale.y ) ),
                static_cast<uint32_t>( std::lround( ( maxX - minX ) * scale.x ) ), static_cast<uint32_t>( std::lround( ( maxY - minY ) * scale.y ) ) );
            wait.Baseline = GetEngine().GetRenderer().GetScreenshotCount();
            wait.Frames = 120;
            wait.Path = parts[1];
            GetEngine().GetRenderer().RequestScreenshot( parts[1] );
            OutRetry = true;
            return true;
        }

        if( query.empty() )
        {
            OutError = "name an item (imgui-dump lists them)";
            return false;
        }

        if( InCommand == "imgui-assert-item" )
        {
            const bool want = parts.size() < 2 || parts[1] != "false";
            std::string error;
            const bool found = MeWidgets::ItemLog::Find( query, error ) != nullptr;
            if( found != want )
            {
                OutError = want ? error : "'" + query + "' is still drawn";
                return false;
            }
            return true;
        }

        if( InCommand == "imgui-assert-value" )
        {
            if( parts.size() < 2 )
            {
                OutError = "usage: imgui-assert-value <item> | <value> [~ tol]";
                return false;
            }
            std::string expected = parts[1];
            float tolerance = 0.001f;
            const size_t tilde = expected.find( '~' );
            if( tilde != std::string::npos )
            {
                tolerance = std::strtof( expected.c_str() + tilde + 1, nullptr );
                expected = Trim( expected.substr( 0, tilde ) );
            }
            std::string error;
            const Item* item = MeWidgets::ItemLog::Find( query, error );
            if( !item )
            {
                OutError = error;
                return false;
            }
            std::string actual = item->Value;
            if( actual.empty() && ( item->Flags & ImGuiItemStatusFlags_Checkable ) )
            {
                actual = ( item->Flags & ImGuiItemStatusFlags_Checked ) ? "true" : "false";
            }
            if( actual.empty() )
            {
                OutError = "'" + query + "' doesn't report a value";
                return false;
            }
            if( !NearlyEqualText( actual, expected, tolerance ) )
            {
                OutError = "'" + query + "' shows '" + actual + "', expected '" + expected + "'";
                return false;
            }
            return true;
        }

        const Item* item = Resolve( query, OutError, OutRetry );
        if( !item )
        {
            return OutRetry;
        }
        const ImVec2 center = item->Rect.GetCenter();
        std::vector<Step> steps;
        std::vector<ImGuiKey> mods;

        if( InCommand == "imgui-hover" )
        {
            Hover( steps, center );
        }
        else if( InCommand == "imgui-click" )
        {
            int button = ImGuiMouseButton_Left;
            bool isDouble = false;
            for( size_t i = 1; i < parts.size(); ++i )
            {
                std::vector<std::string> words = Words( parts[i] );
                std::vector<std::string> modWords;
                for( const std::string& word : words )
                {
                    if( word == "right" ) button = ImGuiMouseButton_Right;
                    else if( word == "double" ) isDouble = true;
                    else if( word != "left" ) modWords.push_back( word );
                }
                if( !ParseModifiers( modWords, mods, OutError ) ) return false;
            }
            Hover( steps, center );
            Click( steps, center, button, mods );
            if( isDouble )
            {
                // Frames in a headless run can be slow: the double-click window is widened for
                // this gesture only.
                gesture.SavedDoubleClickTime = ImGui::GetIO().MouseDoubleClickTime;
                ImGui::GetIO().MouseDoubleClickTime = 5.f;
                Click( steps, center, button, mods );
            }
        }
        else if( InCommand == "imgui-drag" || InCommand == "imgui-scrub" )
        {
            if( parts.size() < 2 )
            {
                OutError = InCommand == "imgui-drag" ? "usage: imgui-drag <item> | <dx> <dy>" : "usage: imgui-scrub <item> | <dx>";
                return false;
            }
            const std::vector<std::string> words = Words( parts[1] );
            const size_t needed = InCommand == "imgui-drag" ? 2 : 1;
            if( words.size() < needed )
            {
                OutError = "missing offset";
                return false;
            }
            const ImVec2 delta( std::strtof( words[0].c_str(), nullptr ), needed == 2 ? std::strtof( words[1].c_str(), nullptr ) : 0.f );
            if( parts.size() > 2 && !ParseModifiers( Words( parts[2] ), mods, OutError ) )
            {
                return false;
            }
            Hover( steps, center );
            Drag( steps, center, delta, mods );
        }
        else if( InCommand == "imgui-type" )
        {
            if( parts.size() < 2 )
            {
                OutError = "usage: imgui-type <item> | <text>";
                return false;
            }
            Hover( steps, center );
            // Ctrl+click turns drags and sliders into text entry (and just focuses a text field).
            Click( steps, center, ImGuiMouseButton_Left, { ImGuiMod_Ctrl } );
            Step selectAll;
            selectAll.Pos = center;
            selectAll.Keys = { { ImGuiMod_Ctrl, true }, { ImGuiKey_A, true } };
            steps.push_back( selectAll );
            Step selectAllUp;
            selectAllUp.Pos = center;
            selectAllUp.Keys = { { ImGuiKey_A, false }, { ImGuiMod_Ctrl, false } };
            steps.push_back( selectAllUp );
            Step type;
            type.Pos = center;
            type.Text = parts[1];
            steps.push_back( type );
            Step enter;
            enter.Pos = center;
            enter.Keys = { { ImGuiKey_Enter, true } };
            steps.push_back( enter );
            Step enterUp;
            enterUp.Pos = center;
            enterUp.Keys = { { ImGuiKey_Enter, false } };
            steps.push_back( enterUp );
            Step settle;
            settle.Pos = center;
            steps.push_back( settle );
        }
        else
        {
            OutError = "unknown command";
            return false;
        }

        Begin( std::move( steps ) );
        ApplyStep( gesture.Steps[gesture.Next++] );
        OutRetry = gesture.Next < gesture.Steps.size();
        if( !OutRetry )
        {
            gesture.Active = false;
        }
        return true;
    }
}
#endif
