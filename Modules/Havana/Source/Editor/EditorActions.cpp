#include "EditorActions.h"

#if USING( ME_EDITOR )

#include "CLog.h"
#include <ImGuiTheme.h>
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
    if( DocumentClaims && DocumentExecute && DocumentClaims( InId ) )
    {
        DocumentExecute( InId );
        return true;
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
        // A focused document takes the action regardless of its scene context (Execute routes it).
        const bool claimed = DocumentClaims && DocumentClaims( action.Id );
        if( !claimed && action.Context != ActionContext::Global && IsContextActive && !IsContextActive( action.Context ) )
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
    const bool claimed = DocumentClaims && DocumentClaims( InId );
    if( ImGui::MenuItem( InLabelOverride ? InLabelOverride : action->DisplayName.c_str(), shortcut.empty() ? nullptr : shortcut.c_str(), false, claimed || IsEnabled( InId ) ) )
    {
        Execute( InId );
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
    ImGui::SetNextWindowPos( ImVec2( viewport->Pos.x + viewport->Size.x * 0.5f, viewport->Pos.y + ImGuiTheme::Em( 4.5f ) ), ImGuiCond_Always, ImVec2( 0.5f, 0.f ) );
    ImGui::SetNextWindowSize( ImVec2( ImGuiTheme::Em( 29.f ), 0.f ) );
    if( !ImGui::BeginPopup( "Command Palette" ) )
    {
        return;
    }

    bool scrollToSelection = ImGui::IsWindowAppearing();
    if( scrollToSelection )
    {
        ImGui::SetKeyboardFocusHere();
    }
    int selectionDelta = 0;
    auto navigate = []( ImGuiInputTextCallbackData* data ) {
        *static_cast<int*>( data->UserData ) = data->EventKey == ImGuiKey_DownArrow ? 1 : -1;
        return 0;
    };
    ImGui::SetNextItemWidth( -1.f );
    const bool submitted = ImGui::InputTextWithHint( "##PaletteFilter", "Type a command...", m_paletteFilter, sizeof( m_paletteFilter ),
        ImGuiInputTextFlags_EnterReturnsTrue | ImGuiInputTextFlags_CallbackHistory, navigate, &selectionDelta );
    if( ImGui::IsItemEdited() )
    {
        m_paletteSelection = 0;
        scrollToSelection = true;
    }

    // Searched when the text changes (and once a second for new entities and files), not every frame.
    if( m_cachedFilter != m_paletteFilter || ImGui::GetTime() - m_cachedAt > 1.0 || scrollToSelection )
    {
        m_cachedFilter = m_paletteFilter;
        m_cachedAt = ImGui::GetTime();
        m_cachedResults = Search( m_paletteFilter );
    }
    const std::vector<PaletteItem>& matches = m_cachedResults;

    m_paletteSelection += selectionDelta;
    scrollToSelection |= selectionDelta != 0;
    m_paletteSelection = matches.empty() ? 0 : std::clamp( m_paletteSelection, 0, static_cast<int>( matches.size() ) - 1 );

    const PaletteItem* chosen = nullptr;
    ImGui::BeginChild( "##PaletteResults", ImVec2( 0.f, std::min( 12, static_cast<int>( matches.size() ) ) * ImGui::GetTextLineHeightWithSpacing() + ImGui::GetStyle().ItemSpacing.y ) );
    for( int i = 0; i < static_cast<int>( matches.size() ); ++i )
    {
        const PaletteItem& item = matches[i];
        const std::string label = item.Category + ": " + item.Label;
        ImGui::PushID( i );
        ImGui::BeginDisabled( !item.Enabled );
        if( ImGui::Selectable( label.c_str(), i == m_paletteSelection ) )
        {
            chosen = &item;
        }
        ImGui::EndDisabled();
        ImGui::PopID();
        // Follow keyboard selection changes without fighting mouse-wheel scrolling.
        if( scrollToSelection && i == m_paletteSelection )
        {
            ImGui::SetScrollHereY();
        }
        if( !item.Shortcut.empty() )
        {
            ImGui::SameLine( ImGui::GetContentRegionAvail().x - ImGui::CalcTextSize( item.Shortcut.c_str() ).x + ImGui::GetCursorPosX() - ImGuiTheme::PanelInset() );
            ImGui::TextDisabled( "%s", item.Shortcut.c_str() );
        }
    }
    ImGui::EndChild();

    if( submitted && !matches.empty() )
    {
        chosen = &matches[m_paletteSelection];
    }
    if( ImGui::IsKeyPressed( ImGuiKey_Escape ) )
    {
        ImGui::CloseCurrentPopup();
    }
    if( chosen && chosen->Enabled && chosen->Run )
    {
        ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
        const std::function<void()> run = chosen->Run;
        run();
        return;
    }
    ImGui::EndPopup();
}

std::vector<EditorActions::PaletteItem> EditorActions::Search( const std::string& InFilter, size_t InMax ) const
{
    std::vector<std::pair<int, PaletteItem>> scored;
    for( const EditorAction& action : m_actions )
    {
        if( !action.Execute || action.HideInPalette )
        {
            continue;
        }
        const int score = FuzzyScore( action.Category + " " + action.DisplayName, InFilter );
        if( score >= 0 )
        {
            PaletteItem item;
            item.Category = action.Category;
            item.Label = action.DisplayName;
            item.Run = action.Execute;
            item.Shortcut = ShortcutToString( action.Shortcut );
            item.Enabled = !action.IsEnabled || action.IsEnabled();
            scored.emplace_back( score, std::move( item ) );
        }
    }
    // The rest of the editor only once something is typed (assets alone can be thousands).
    if( !InFilter.empty() )
    {
        std::vector<PaletteItem> found;
        for( const auto& provider : SearchProviders )
        {
            provider( found );
        }
        for( PaletteItem& item : found )
        {
            const int score = FuzzyScore( item.Category + " " + item.Label, InFilter );
            if( score >= 0 )
            {
                // Behind an action that matches as well.
                scored.emplace_back( score + 1, std::move( item ) );
            }
        }
    }
    std::stable_sort( scored.begin(), scored.end(), []( const auto& a, const auto& b ) { return a.first < b.first; } );
    std::vector<PaletteItem> items;
    for( auto& [score, item] : scored )
    {
        if( items.size() >= InMax )
        {
            break;
        }
        items.push_back( std::move( item ) );
    }
    return items;
}

#endif
