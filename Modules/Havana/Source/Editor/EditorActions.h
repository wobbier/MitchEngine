#pragma once
#include "Dementia.h"
#include <imgui.h>
#include <cstdint>
#include <functional>
#include <string>
#include <unordered_map>
#include <vector>

#if USING( ME_EDITOR )

// Where an action's shortcut is live.
enum class ActionContext : uint8_t
{
    Global,         // Anywhere in the editor (Save, Undo, Play...).
    Scene,          // Only while the hierarchy or a scene view has focus (Delete, Copy, Duplicate...).
};

// A named editor action ("Edit.Undo") with optional keyboard shortcuts. Menus, the command
// palette, shortcuts and editor automation scripts all invoke actions by id.
// Use ImGuiMod_Shortcut for Ctrl (Cmd on macOS).
struct EditorAction
{
    std::string Id;
    std::string DisplayName;
    std::string Category;
    ImGuiKeyChord Shortcut = 0;
    ImGuiKeyChord AltShortcut = 0;
    std::function<void()> Execute;
    std::function<bool()> IsEnabled;
    ActionContext Context = ActionContext::Global;
    // Shortcut also fires while a text field has focus (e.g. Save).
    bool AllowInTextInput = false;
    // Shortcut also fires while the game view owns input (e.g. Stop).
    bool AllowWhileGameFocused = false;
    // Hidden from the command palette (internal / automation-only actions).
    bool HideInPalette = false;
    // The shortcut the action was registered with (before user rebinding).
    ImGuiKeyChord DefaultShortcut = 0;
};

class EditorActions
{
public:
    static EditorActions& Get();

    void Register( EditorAction InAction );

    // User rebinding. Overrides are applied to actions registered later too.
    void SetShortcut( const std::string& InId, ImGuiKeyChord InChord );
    void ResetShortcut( const std::string& InId );
    void SetShortcutOverrides( const std::unordered_map<std::string, ImGuiKeyChord>& InOverrides );
    const std::unordered_map<std::string, ImGuiKeyChord>& GetShortcutOverrides() const { return m_overrides; }
    // Another action already using InChord (null if free).
    const EditorAction* FindConflict( const std::string& InId, ImGuiKeyChord InChord ) const;
    const EditorAction* Find( const std::string& InId ) const;
    bool Execute( const std::string& InId );
    bool IsEnabled( const std::string& InId ) const;

    // Fires actions whose shortcut was pressed this frame (call once per frame after NewFrame).
    void ProcessShortcuts();

    // Editor state the shortcut filter needs (set by Havana).
    std::function<bool( ActionContext )> IsContextActive;
    std::function<bool()> IsGameFocused;
    // The focused document window (e.g. a shader graph) can take over actions such as Save, Undo,
    // Copy or Delete while it has focus: DocumentClaims says whether it wants InId right now, and
    // DocumentExecute runs it. Shortcuts, menus, the palette and automation all route through it.
    std::function<bool( const std::string& )> DocumentClaims;
    std::function<void( const std::string& )> DocumentExecute;
    // Set while a shortcut is being captured for rebinding.
    bool ShortcutsSuspended = false;

    void OpenPalette() { m_openPalette = true; }
    void DrawPalette();

    // ED-15: search everywhere. Providers add what the palette can find besides actions (entities,
    // assets, settings, docs) once something is typed; results are fuzzy-matched with the actions.
    struct PaletteItem
    {
        std::string Category;
        std::string Label;
        std::function<void()> Run;
        std::string Shortcut;
        bool Enabled = true;
    };
    std::vector<std::function<void( std::vector<PaletteItem>& OutItems )>> SearchProviders;
    // What the palette lists for InFilter, best first (actions, then providers' items), capped.
    std::vector<PaletteItem> Search( const std::string& InFilter, size_t InMax = 200 ) const;

    // "Ctrl+Shift+Z"
    static std::string ShortcutToString( ImGuiKeyChord InChord );
    // Replaces ImGuiMod_Shortcut with the platform's modifier.
    static ImGuiKeyChord ResolveChord( ImGuiKeyChord InChord );

    // MenuItem bound to an action (label, shortcut text, enabled state).
    bool MenuItem( const std::string& InId, const char* InLabelOverride = nullptr );

    const std::vector<EditorAction>& GetAll() const { return m_actions; }

private:
    std::vector<EditorAction> m_actions;
    std::unordered_map<std::string, ImGuiKeyChord> m_overrides;
    bool m_openPalette = false;
    char m_paletteFilter[128] = {};
    int m_paletteSelection = 0;
    std::string m_cachedFilter;
    double m_cachedAt = -100.0;
    std::vector<PaletteItem> m_cachedResults;
};

#endif
