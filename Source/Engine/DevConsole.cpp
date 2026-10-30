#include "PCH.h"
#include "Engine/DevConsole.h"
#include "CLog.h"
#include "Core/CommandLine.h"
#include "Engine/Engine.h"
#if USING( ME_SCRIPTING )
#include "Scripting/ScriptEngine.h"
#endif
#if USING( ME_IMGUI )
#include <imgui.h>
#endif
#include <algorithm>
#include <cctype>
#include <sstream>


DevConsole& DevConsole::Get()
{
    static DevConsole instance;
    return instance;
}


DevConsole::DevConsole()
{
    Register( "help", "Lists commands (help <prefix> narrows it)", [this]( const std::vector<std::string>& InArgs ) {
        std::string out;
        const std::string prefix = InArgs.empty() ? std::string() : InArgs[0];
        for( const auto& [name, command] : m_commands )
        {
            if( name.rfind( prefix, 0 ) == 0 )
            {
                out += name + " - " + command.Help + "\n";
            }
        }
#if USING( ME_SCRIPTING )
        for( const auto& [name, help] : ScriptEngine::GetConsoleCommands() )
        {
            if( name.rfind( prefix, 0 ) == 0 )
            {
                out += name + " - " + help + " (C#)\n";
            }
        }
#endif
        return out;
    } );
    Register( "echo", "Prints its arguments", []( const std::vector<std::string>& InArgs ) {
        std::string out;
        for( const std::string& arg : InArgs )
        {
            out += ( out.empty() ? "" : " " ) + arg;
        }
        return out;
    } );
    Register( "clear", "Clears the console", [this]( const std::vector<std::string>& ) {
        m_history.clear();
        return std::string();
    } );
    RegisterVariable( "timescale", "Game time scale (1 = normal)", []() { return std::to_string( GetEngine().GetTimeScale() ); },
        []( const std::string& InValue ) {
            char* end = nullptr;
            const float value = std::strtof( InValue.c_str(), &end );
            if( end == InValue.c_str() || value < 0.f )
            {
                return false;
            }
            GetEngine().SetTimeScale( value );
            return true;
        } );
    RegisterVariable( "maxfps", "Frame rate cap (0 = uncapped)", []() { return std::to_string( GetEngine().GetMaxFrameRate() ); },
        []( const std::string& InValue ) {
            char* end = nullptr;
            const float value = std::strtof( InValue.c_str(), &end );
            if( end == InValue.c_str() || value < 0.f )
            {
                return false;
            }
            GetEngine().SetMaxFrameRate( value );
            return true;
        } );
}


void DevConsole::Register( const std::string& InName, const std::string& InHelp, Handler InHandler )
{
    m_commands[InName] = Command{ InHelp, std::move( InHandler ) };
}


void DevConsole::RegisterVariable( const std::string& InName, const std::string& InHelp, std::function<std::string()> InGet,
    std::function<bool( const std::string& InValue )> InSet )
{
    Register( InName, InHelp + " (variable)", [InName, InGet, InSet]( const std::vector<std::string>& InArgs ) {
        if( !InArgs.empty() && !InSet( InArgs[0] ) )
        {
            return InName + ": '" + InArgs[0] + "' isn't a valid value";
        }
        return InName + " = " + InGet();
    } );
}


std::vector<std::string> DevConsole::Split( const std::string& InLine )
{
    std::vector<std::string> words;
    std::string word;
    bool quoted = false;
    bool has = false;
    for( char c : InLine )
    {
        if( c == '"' )
        {
            quoted = !quoted;
            has = true;
        }
        else if( std::isspace( static_cast<unsigned char>( c ) ) && !quoted )
        {
            if( has )
            {
                words.push_back( word );
                word.clear();
                has = false;
            }
        }
        else
        {
            word += c;
            has = true;
        }
    }
    if( has )
    {
        words.push_back( word );
    }
    return words;
}


std::string DevConsole::Execute( const std::string& InLine )
{
    std::vector<std::string> words = Split( InLine );
    if( words.empty() )
    {
        return {};
    }
    const std::string name = words[0];
    words.erase( words.begin() );
    std::string out;
    if( const auto found = m_commands.find( name ); found != m_commands.end() )
    {
        out = found->second.Run( words );
    }
    else
    {
        bool handled = false;
#if USING( ME_SCRIPTING )
        handled = ScriptEngine::RunConsoleCommand( InLine, out );
#endif
        if( !handled )
        {
            out = "Unknown command '" + name + "' (help lists them)";
        }
    }
    while( !out.empty() && out.back() == '\n' )
    {
        out.pop_back();
    }
    m_history.push_back( "> " + InLine );
    if( !out.empty() )
    {
        m_history.push_back( out );
    }
    if( m_history.size() > 500 )
    {
        m_history.erase( m_history.begin(), m_history.begin() + ( m_history.size() - 500 ) );
    }
    m_lastOutput = out;
    CLog::Log( CLog::LogType::Info, "Console: > " + InLine + ( out.empty() ? "" : "\n" + out ) );
    return out;
}


std::vector<std::string> DevConsole::Complete( const std::string& InPrefix ) const
{
    std::vector<std::string> names;
    for( const auto& [name, command] : m_commands )
    {
        if( name.rfind( InPrefix, 0 ) == 0 )
        {
            names.push_back( name );
        }
    }
#if USING( ME_SCRIPTING )
    for( const auto& [name, help] : ScriptEngine::GetConsoleCommands() )
    {
        if( name.rfind( InPrefix, 0 ) == 0 )
        {
            names.push_back( name );
        }
    }
#endif
    std::sort( names.begin(), names.end() );
    names.erase( std::unique( names.begin(), names.end() ), names.end() );
    return names;
}


void DevConsole::UpdateAndDraw( bool InTogglePressed )
{
#if USING( ME_IMGUI ) && !USING( ME_RETAIL )
    // --console "<line>": open at start-up and run the line (captures, quick checks).
    static bool s_startupLine = false;
    if( !s_startupLine )
    {
        s_startupLine = true;
        if( CommandLine::Has( "--console" ) )
        {
            m_open = true;
            Execute( CommandLine::GetString( "--console" ) );
        }
    }
    if( InTogglePressed )
    {
        m_open = !m_open;
        m_focusInput = m_open;
    }
    if( !m_open )
    {
        return;
    }
    const ImGuiViewport* viewport = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos( viewport->Pos, ImGuiCond_Always );
    ImGui::SetNextWindowSize( ImVec2( viewport->Size.x, viewport->Size.y * 0.4f ), ImGuiCond_Always );
    if( ImGui::Begin( "Console", &m_open, ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoCollapse ) )
    {
        const float footer = ImGui::GetFrameHeightWithSpacing();
        ImGui::BeginChild( "##ConsoleHistory", ImVec2( 0.f, -footer ) );
        for( const std::string& line : m_history )
        {
            ImGui::TextUnformatted( line.c_str() );
        }
        if( ImGui::GetScrollY() >= ImGui::GetScrollMaxY() )
        {
            ImGui::SetScrollHereY( 1.f );
        }
        ImGui::EndChild();
        ImGui::SetNextItemWidth( -1.f );
        if( m_focusInput )
        {
            ImGui::SetKeyboardFocusHere();
            m_focusInput = false;
        }
        // Tab completes the command name (the longest common prefix of the matches).
        auto complete = []( ImGuiInputTextCallbackData* InData ) {
            if( InData->EventFlag != ImGuiInputTextFlags_CallbackCompletion )
            {
                return 0;
            }
            const std::string typed( InData->Buf, InData->BufTextLen );
            if( typed.find( ' ' ) != std::string::npos )
            {
                return 0;
            }
            const std::vector<std::string> matches = DevConsole::Get().Complete( typed );
            if( matches.empty() )
            {
                return 0;
            }
            std::string common = matches.front();
            for( const std::string& match : matches )
            {
                size_t length = 0;
                while( length < common.size() && length < match.size() && common[length] == match[length] )
                {
                    ++length;
                }
                common.resize( length );
            }
            if( matches.size() == 1 )
            {
                common += " ";
            }
            InData->DeleteChars( 0, InData->BufTextLen );
            InData->InsertChars( 0, common.c_str() );
            return 0;
        };
        if( ImGui::InputText( "##ConsoleInput", m_input, sizeof( m_input ), ImGuiInputTextFlags_EnterReturnsTrue | ImGuiInputTextFlags_CallbackCompletion, complete ) )
        {
            Execute( m_input );
            m_input[0] = 0;
            m_focusInput = true;
        }
    }
    ImGui::End();
#else
    (void)InTogglePressed;
#endif
}
