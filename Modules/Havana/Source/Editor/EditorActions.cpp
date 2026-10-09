#include "EditorActions.h"

#if USING( ME_EDITOR )

#include "CLog.h"
#include <algorithm>
#include <cctype>


EditorActions& EditorActions::Get()
{
    static EditorActions instance;
    return instance;
}


void EditorActions::Register( EditorAction InAction )
{
    InAction.DefaultShortcut = InAction.Shortcut;
    auto overridden = m_overrides.find( InAction.Id );
    if( overridden != m_overrides.end() )
    {
        InAction.Shortcut = overridden->second;
    }
    auto existing = std::find_if( m_actions.begin(), m_actions.end(), [&InAction]( const EditorAction& action ) { return action.Id == InAction.Id; } );
    if( existing != m_actions.end() )
    {
        *existing = std::move( InAction );
        return;
    }
    m_actions.push_back( std::move( InAction ) );
}


void EditorActions::SetShortcut( const std::string& InId, ImGuiKeyChord InChord )
{
    m_overrides[InId] = InChord;
    for( EditorAction& action : m_actions )
    {
        if( action.Id == InId )
        {
            action.Shortcut = InChord;
        }
    }
}


void EditorActions::ResetShortcut( const std::string& InId )
{
    m_overrides.erase( InId );
    for( EditorAction& action : m_actions )
    {
        if( action.Id == InId )
        {
            action.Shortcut = action.DefaultShortcut;
        }
    }
}


void EditorActions::SetShortcutOverrides( const std::unordered_map<std::string, ImGuiKeyChord>& InOverrides )
{
    m_overrides = InOverrides;
    for( EditorAction& action : m_actions )
    {
        auto overridden = m_overrides.find( action.Id );
        action.Shortcut = overridden != m_overrides.end() ? overridden->second : action.DefaultShortcut;
    }
}


const EditorAction* EditorActions::FindConflict( const std::string& InId, ImGuiKeyChord InChord ) const
{
    if( InChord == 0 )
    {
        return nullptr;
    }
    const ImGuiKeyChord resolved = ResolveChord( InChord );
    for( const EditorAction& action : m_actions )
    {
        if( action.Id != InId && ( ResolveChord( action.Shortcut ) == resolved || ( action.AltShortcut && ResolveChord( action.AltShortcut ) == resolved ) ) )
        {
            return &action;
        }
    }
    return nullptr;
}


const EditorAction* EditorActions::Find( const std::string& InId ) const
{
    auto found = std::find_if( m_actions.begin(), m_actions.end(), [&InId]( const EditorAction& action ) { return action.Id == InId; } );
    return found != m_actions.end() ? &*found : nullptr;
}


bool EditorActions::IsEnabled( const std::string& InId ) const
{
    const EditorAction* action = Find( InId );
    return action && action->Execute && ( !action->IsEnabled || action->IsEnabled() );
}


bool EditorActions::Execute( const std::string& InId )
{
    const EditorAction* action = Find( InId );
    if( !action )
    {
        BRUH( "Unknown editor action: " + InId );
        return false;
    }
    if( !IsEnabled( InId ) )
    {
        return false;
    }
    action->Execute();
    return true;
}


void EditorActions::ProcessShortcuts()
{
    if( ShortcutsSuspended )
    {
        return;
    }
    const ImGuiIO& io = ImGui::GetIO();
    const bool gameFocused = IsGameFocused && IsGameFocused();
    const ImGuiKeyChord currentMods = io.KeyMods & ImGuiMod_Mask_;

    auto pressed = [currentMods]( ImGuiKeyChord InChord ) {
        if( InChord == 0 )
        {
            return false;
        }
        const ImGuiKeyChord chord = ResolveChord( InChord );
        // Exact modifier match: Ctrl+Z must not fire on Ctrl+Shift+Z.
        if( ( chord & ImGuiMod_Mask_ ) != currentMods )
        {
            return false;
        }
        return ImGui::IsKeyPressed( static_cast<ImGuiKey>( chord & ~ImGuiMod_Mask_ ), false );
    };

    // Collect first: an action may register/replace actions while executing.
    std::vector<std::string> fired;
    for( const EditorAction& action : m_actions )
    {
        if( !action.Execute || ( io.WantTextInput && !action.AllowInTextInput ) || ( gameFocused && !action.AllowWhileGameFocused ) )
        {
            continue;
        }
        if( action.Context != ActionContext::Global && IsContextActive && !IsContextActive( action.Context ) )
        {
            continue;
        }
        if( pressed( action.Shortcut ) || pressed( action.AltShortcut ) )
        {
            fired.push_back( action.Id );
        }
    }
    for( const std::string& id : fired )
    {
        Execute( id );
    }
}


ImGuiKeyChord EditorActions::ResolveChord( ImGuiKeyChord InChord )
{
    if( InChord & ImGuiMod_Shortcut )
    {
        InChord &= ~ImGuiMod_Shortcut;
        InChord |= ImGui::GetIO().ConfigMacOSXBehaviors ? ImGuiMod_Super : ImGuiMod_Ctrl;
    }
    return InChord;
}


std::string EditorActions::ShortcutToString( ImGuiKeyChord InChord )
{
    if( InChord == 0 )
    {
        return {};
    }
    InChord = ResolveChord( InChord );
    std::string text;
    if( InChord & ImGuiMod_Ctrl ) text += "Ctrl+";
    if( InChord & ImGuiMod_Super ) text += ImGui::GetIO().ConfigMacOSXBehaviors ? "Cmd+" : "Super+";
    if( InChord & ImGuiMod_Shift ) text += "Shift+";
    if( InChord & ImGuiMod_Alt ) text += "Alt+";
    const ImGuiKey key = static_cast<ImGuiKey>( InChord & ~ImGuiMod_Mask_ );
    text += ImGui::GetKeyName( key );
    return text;
}


bool EditorActions::MenuItem( const std::string& InId, const char* InLabelOverride )
{
    const EditorAction* action = Find( InId );
    if( !action )
    {
        return false;
    }
    const std::string shortcut = ShortcutToString( action->Shortcut );
    if( ImGui::MenuItem( InLabelOverride ? InLabelOverride : action->DisplayName.c_str(), shortcut.empty() ? nullptr : shortcut.c_str(), false, IsEnabled( InId ) ) )
    {
        action->Execute();
        return true;
    }
    return false;
}


namespace
{
    // Subsequence match ("dupsel" matches "Duplicate Selection"); lower score is better.
    int FuzzyScore( const std::string& InText, const std::string& InPattern )
    {
        if( InPattern.empty() )
        {
            return 0;
        }
        size_t position = 0;
        int score = 0;
        for( char c : InPattern )
        {
            const char lower = static_cast<char>( std::tolower( static_cast<unsigned char>( c ) ) );
            bool found = false;
            for( ; position < InText.size(); ++position )
            {
                if( std::tolower( static_cast<unsigned char>( InText[position] ) ) == lower )
                {
                    found = true;
                    ++position;
                    break;
                }
                ++score;
            }
            if( !found )
            {
                return -1;
            }
        }
        return score;
    }
}


void EditorActions::DrawPalette()
{
    if( m_openPalette )
    {
        ImGui::OpenPopup( "Command Palette" );
        m_openPalette = false;
        m_paletteFilter[0] = '\0';
        m_paletteSelection = 0;
    }

    const ImGuiViewport* viewport = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos( ImVec2( viewport->Pos.x + viewport->Size.x * 0.5f, viewport->Pos.y + 80.f ), ImGuiCond_Always, ImVec2( 0.5f, 0.f ) );
    ImGui::SetNextWindowSize( ImVec2( 520.f, 0.f ) );
    if( !ImGui::BeginPopup( "Command Palette" ) )
    {
        return;
    }

    if( ImGui::IsWindowAppearing() )
    {
        ImGui::SetKeyboardFocusHere();
    }
    ImGui::SetNextItemWidth( -1.f );
    const bool submitted = ImGui::InputTextWithHint( "##PaletteFilter", "Type a command...", m_paletteFilter, sizeof( m_paletteFilter ), ImGuiInputTextFlags_EnterReturnsTrue );

    std::vector<std::pair<int, const EditorAction*>> matches;
    for( const EditorAction& action : m_actions )
    {
        if( !action.Execute || action.HideInPalette )
        {
            continue;
        }
        const int score = FuzzyScore( action.Category + " " + action.DisplayName, m_paletteFilter );
        if( score >= 0 )
        {
            matches.emplace_back( score, &action );
        }
    }
    std::stable_sort( matches.begin(), matches.end(), []( const auto& a, const auto& b ) { return a.first < b.first; } );

    if( ImGui::IsKeyPressed( ImGuiKey_DownArrow ) )
    {
        ++m_paletteSelection;
    }
    if( ImGui::IsKeyPressed( ImGuiKey_UpArrow ) )
    {
        --m_paletteSelection;
    }
    m_paletteSelection = matches.empty() ? 0 : std::clamp( m_paletteSelection, 0, static_cast<int>( matches.size() ) - 1 );

    const EditorAction* chosen = nullptr;
    ImGui::BeginChild( "##PaletteResults", ImVec2( 0.f, std::min( 12, static_cast<int>( matches.size() ) ) * ImGui::GetTextLineHeightWithSpacing() + 4.f ) );
    for( int i = 0; i < static_cast<int>( matches.size() ); ++i )
    {
        const EditorAction& action = *matches[i].second;
        const bool enabled = !action.IsEnabled || action.IsEnabled();
        const std::string label = action.Category + ": " + action.DisplayName;
        ImGui::BeginDisabled( !enabled );
        if( ImGui::Selectable( label.c_str(), i == m_paletteSelection ) )
        {
            chosen = &action;
        }
        ImGui::EndDisabled();
        const std::string shortcut = ShortcutToString( action.Shortcut );
        if( !shortcut.empty() )
        {
            ImGui::SameLine( ImGui::GetContentRegionAvail().x - ImGui::CalcTextSize( shortcut.c_str() ).x + ImGui::GetCursorPosX() - 8.f );
            ImGui::TextDisabled( "%s", shortcut.c_str() );
        }
        if( i == m_paletteSelection )
        {
            ImGui::SetScrollHereY();
        }
    }
    ImGui::EndChild();

    if( submitted && !matches.empty() )
    {
        chosen = matches[m_paletteSelection].second;
    }
    if( ImGui::IsKeyPressed( ImGuiKey_Escape ) )
    {
        ImGui::CloseCurrentPopup();
    }
    if( chosen && ( !chosen->IsEnabled || chosen->IsEnabled() ) )
    {
        ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
        chosen->Execute();
        return;
    }
    ImGui::EndPopup();
}

#endif
