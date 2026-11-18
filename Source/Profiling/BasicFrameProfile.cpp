#include "PCH.h"
#include "BasicFrameProfile.h"

#if USING( ME_BASIC_PROFILER )

#include <imgui.h>
#include <Engine/Engine.h>
#include "Core/CommandLine.h"
#include "UI/Colors.h"
#include <chrono>
#include <cstdio>
#include <algorithm>

FrameProfile::FrameProfile()
{
}

void FrameProfile::Start()
{
}

void FrameProfile::End( float frameDelta )
{
    MainDelta = frameDelta;

    // The readout's numbers: refreshed every kFpsWindowSeconds so they're readable, not flickering.
    WindowSeconds += frameDelta;
    WindowWorstSeconds = std::max( WindowWorstSeconds, frameDelta );
    ++WindowFrames;
    if( WindowSeconds >= kFpsWindowSeconds )
    {
        DisplayFps = static_cast<float>( WindowFrames ) / WindowSeconds;
        DisplayAverageMs = WindowSeconds * 1000.f / static_cast<float>( WindowFrames );
        DisplayWorstMs = WindowWorstSeconds * 1000.f;
        WindowSeconds = 0.f;
        WindowFrames = 0;
        WindowWorstSeconds = 0.f;
    }
}

void FrameProfile::Set( const std::string& name, ProfileCategory category )
{
    OPTICK_EVENT( "FrameProfile::Set" );
    Profiles[name].Category = category;
    Profiles[name].Timer.Reset();
}

void FrameProfile::Complete( const std::string& name )
{
    OPTICK_EVENT( "FrameProfile::Complete" );
    Profiles[name].Timer.Update();
}

bool sortByVal( const std::pair<std::string, ProfileInfo>& a,
    const std::pair<std::string, ProfileInfo>& b )
{
    return ( a.second.Category < b.second.Category );
}

void FrameProfile::Render( const Vector2& inPosition, const Vector2& inSize )
{
    if( CommandLine::Has( "--no-debug-overlay" ) )
    {
        return;
    }

    const ImU32 flags = ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoScrollbar /*| ImGuiWindowFlags_NoInputs*/ | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoFocusOnAppearing | ImGuiWindowFlags_NoBringToFrontOnFocus;
    ImGui::SetNextWindowSize( ImVec2( inSize.x, (float)CurrentProfilerSize ) );
    ImGui::SetNextWindowPos( ImVec2( inPosition.x, inPosition.y - ( ( CurrentProfilerSize == kMinProfilerSize ) ? 0 : CurrentProfilerSize - 6.f ) ) );

    ImGui::PushStyleColor( ImGuiCol_Border, 0 );
    ImGui::PushStyleVar( ImGuiStyleVar_WindowPadding, ImVec2( 0.f, 0.f ) );
    ImGui::PushStyleVar( ImGuiStyleVar_WindowRounding, 0.0f );
    ImGui::PushStyleVar( ImGuiStyleVar_WindowMinSize, ImVec2( 2.f, 5.f ) );
    ImGui::PushStyleVar( ImGuiStyleVar_ItemSpacing, ImVec2( 0.f, 0.f ) );
    ImGui::PushStyleColor( ImGuiCol_WindowBg, { 0.f, 0.f, 0.f, 0.f } );

    std::vector<std::pair<std::string, ProfileInfo>> profileShit;
    float totalFrameTime = 0.f;
    for( auto& thing : FrameProfile::GetInstance().ProfileDump )
    {
        totalFrameTime += thing.second.Timer.GetDeltaSeconds();
        profileShit.push_back( std::make_pair( thing.first, thing.second ) );
    }
    std::sort( profileShit.begin(), profileShit.end(), sortByVal );

    totalFrameTime = FrameProfile::GetInstance().MainDelta;

    ImGui::Begin( "FrameProfileBar", NULL, flags );
    {
        ImGui::PopStyleColor();
        ImDrawList* draw_list = ImGui::GetWindowDrawList();

        const ImVec2 p = ImGui::GetCursorScreenPos();
        static ImVec4 col2 = ImVec4( 1.0f, .5f, 1.0f, 1.0f );
        static ImVec4 col3 = ImVec4( .5f, 1.f, 1.0f, 1.0f );
        {
            ImU32 col322 = ImColor( col2 );
            ImU32 col323 = ImColor( col3 );
            float x = p.x, y = p.y;

            float sz = 50.f;
            //draw_list->AddRectFilled(ImVec2(x, y), ImVec2(x + ImGui::GetWindowSize().x, y + sz), col322);
            float previousX = 0;
            // Bar scale: a frame at the cap (or 144 Hz when uncapped) fills the bar.
            const float targetFPS = 1.f / ( GetEngine().GetMaxFrameRate() > 0.f ? GetEngine().GetMaxFrameRate() : 144.f );
            float size = ( totalFrameTime / targetFPS );

            if( size >= 100.f )
            {
                size /= 100.f;
            }

            // Full window size
            float windowSizeX = ImGui::GetMainViewport()->Size.x;

            // How big is the whole frame UI wise
            float profileSize = ( windowSizeX * size );
            for( auto& thing : profileShit )
            {
                ImVec4 profileColor = FrameProfile::GetInstance().GetCategoryColor( thing.second.Category );
                ImVec4 col = ImVec4( profileColor.x, profileColor.y, profileColor.z, 1.0f );
                ImU32 col32 = ImColor( col );
                float maxSize = ( profileSize <= windowSizeX ) ? profileSize : windowSizeX;
                float profileSizeX = ( thing.second.Timer.GetDeltaSeconds() / totalFrameTime ) * maxSize;
                draw_list->AddRectFilled( ImVec2( previousX + x, y ), ImVec2( previousX + x + profileSizeX, y + CurrentProfilerSize ), col32 );
                ImGui::SameLine();
                previousX = previousX + profileSizeX;
            }
            if( profileSize > windowSizeX )
            {
                ImVec4 errColor = ACCENT_RED;
                ImU32 col32 = ImColor( errColor );

                float profileSizeX = profileSize / windowSizeX;
                float xxxx = windowSizeX * profileSizeX;
                draw_list->AddRectFilled( ImVec2( x + windowSizeX - ( profileSize - windowSizeX ), y ), ImVec2( ( windowSizeX + xxxx ), y + 10.f ), col32 );
                ImGui::SameLine();
                previousX = previousX + profileSizeX;
            }
            else
            {
                ImVec4 spareColor = ACCENT_GREEN;
                ImU32 col32 = ImColor( spareColor );
                //float xxxx = windowSizeX * profileSize;
                draw_list->AddRectFilled( ImVec2( previousX + x, y ), ImVec2( windowSizeX + x, y + CurrentProfilerSize ), col32 );
            }

            ImGui::PopStyleVar( 4 );
            ImGui::PopStyleColor( 1 );

            // The frame rate readout, just above the bar's left end: fps, average and worst frame.
            if( DisplayFps > 0.f )
            {
                char readout[96];
                std::snprintf( readout, sizeof( readout ), "%.0f fps  %.2f ms  (worst %.2f ms)", DisplayFps, DisplayAverageMs, DisplayWorstMs );
                ImDrawList* foreground = ImGui::GetForegroundDrawList( ImGui::GetWindowViewport() );
                const ImVec2 textSize = ImGui::CalcTextSize( readout );
                const ImVec2 textPos( x + 6.f, y - textSize.y - 8.f );
                foreground->AddRectFilled( ImVec2( textPos.x - 4.f, textPos.y - 2.f ), ImVec2( textPos.x + textSize.x + 4.f, textPos.y + textSize.y + 2.f ), IM_COL32( 0, 0, 0, 170 ), 3.f );
                foreground->AddText( textPos, IM_COL32( 255, 255, 255, 255 ), readout );
            }

            if( ImGui::IsWindowHovered() )
            {
                CurrentProfilerSize = kMaxProfilerSize;
                ImGui::SetNextWindowPos( ImVec2( ImGui::GetMousePos().x, ( ImGui::GetMousePos().y - PreviousTooltipHeight ) - CurrentProfilerSize ) );
                ImGui::BeginTooltip();
                ImGui::PushTextWrapPos( ImGui::GetFontSize() * 35.0f );

                for( auto& thing : profileShit )
                {
                    ImVec4 color = GetCategoryColor( thing.second.Category );
                    ImGui::ColorEdit4( "MyColor##3", &color.x, ImGuiColorEditFlags_NoInputs | ImGuiColorEditFlags_NoLabel );
                    ImGui::SameLine();
                    ImGui::TextUnformatted( thing.first.c_str() );
                    ImGui::SameLine();
                    ImGui::TextUnformatted( std::to_string( thing.second.Timer.GetDeltaSeconds() * 1000.f ).c_str() );
                    ImGui::SameLine();
                    ImGui::TextUnformatted( "ms" );
                }
                ImGui::PopTextWrapPos();

                PreviousTooltipHeight = static_cast<int>( ImGui::GetWindowHeight() );
                ImGui::EndTooltip();
            }
            else
            {
                CurrentProfilerSize = kMinProfilerSize;
            }
        }
    }
    ImGui::End();
}

void FrameProfile::Dump()
{
    ProfileDump = Profiles;
    //std::sort(ProfileDump.begin(), ProfileDump.end(), [](const Clock& First, const Clock& Second) {
    //	return First.GetPreviousTime() < Second.GetPreviousTime();
    //});
}

ImVec4 FrameProfile::GetCategoryColor( ProfileCategory category )
{
    switch( category )
    {
    case ProfileCategory::Game:
        return ACCENT_BLUE;
    case ProfileCategory::UI:
        return ACCENT_PINK;
    case ProfileCategory::Rendering:
        return ACCENT_ORANGE;
    case ProfileCategory::Physics:
        return ACCENT_PURPLE;
    default:
        break;
    }
    return ACCENT_WHITE;
}

#endif
