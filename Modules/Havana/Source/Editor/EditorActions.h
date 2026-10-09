#pragma once
#include "Dementia.h"
#include <imgui.h>
#include <cstdint>
#include <functional>
#include <string>
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
};

class EditorActions
{
public:
    static EditorActions& Get();

    void Register( EditorAction InAction );
    const EditorAction* Find( const std::string& InId ) const;
    bool Execute( const std::string& InId );
    bool IsEnabled( const std::string& InId ) const;

    // Fires actions whose shortcut was pressed this frame (call once per frame after NewFrame).
    void ProcessShortcuts();

    // Editor state the shortcut filter needs (set by Havana).
    std::function<bool( ActionContext )> IsContextActive;
    std::function<bool()> IsGameFocused;

    void OpenPalette() { m_openPalette = true; }
    void DrawPalette();

    // "Ctrl+Shift+Z"
    static std::string ShortcutToString( ImGuiKeyChord InChord );
    // Replaces ImGuiMod_Shortcut with the platform's modifier.
    static ImGuiKeyChord ResolveChord( ImGuiKeyChord InChord );

    // MenuItem bound to an action (label, shortcut text, enabled state).
    bool MenuItem( const std::string& InId, const char* InLabelOverride = nullptr );

    const std::vector<EditorAction>& GetAll() const { return m_actions; }

private:
    std::vector<EditorAction> m_actions;
    bool m_openPalette = false;
    char m_paletteFilter[128] = {};
    int m_paletteSelection = 0;
};

#endif
