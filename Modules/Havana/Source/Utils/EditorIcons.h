#pragma once
#include "Dementia.h"

#if USING( ME_EDITOR )
#include <string>
#include <bgfx/bgfx.h>
#include "imgui.h"

// The editor's icon set: the Tabler outline icons in Assets/Icons/outline/<name>.svg. Each icon is
// rasterized in white at the pixel size it's drawn at (crisp at any font size), so the draw colour
// tints it: text, muted, accent, or faded while disabled. Textures live until Shutdown.
namespace EditorIcons
{
    // The icon at InPixels square, rasterized on first use. Invalid when there's no such icon.
    bgfx::TextureHandle Get( const std::string& InName, float InPixels );
    // Draws the icon into the square at InMin (a "?" when it's missing).
    void Draw( ImDrawList* InDrawList, const std::string& InName, const ImVec2& InMin, float InSize, ImU32 InColor );
    // The icon as an item at the cursor: InSize defaults to the text height, InColor to the text colour.
    void Image( const std::string& InName, float InSize = 0.f, ImU32 InColor = 0 );

    // A square toolbar button as tall as a field. InChecked fills it like a pressed toggle. The
    // tooltip (InLabel, then InDescription) also shows while it's disabled.
    bool Button( const char* InIcon, const char* InLabel, const char* InDescription = nullptr, bool InEnabled = true, bool InChecked = false );
    // Whether unbuilt features are shown (Preferences > General > Show planned tools; off by default).
    bool ShowPlanned();
    // A disabled button for a feature that isn't built yet; its tooltip starts "Planned:". Draws
    // nothing unless ShowPlanned().
    void Planned( const char* InIcon, const char* InLabel, const char* InDescription );
    // The tooltip the buttons use: a title line and a wrapped description.
    void Tooltip( const char* InLabel, const char* InDescription );
    // A menu item with the icon in the menu's icon column (every row of that menu lines up with it,
    // so use it for a whole submenu rather than one row of a plain menu).
    bool MenuItem( const char* InIcon, const char* InLabel, const char* InShortcut = nullptr, bool InSelected = false, bool InEnabled = true );
    // BeginMenu with an icon, for a submenu among MenuItem rows (EndMenu as usual).
    bool BeginMenu( const char* InIcon, const char* InLabel, bool InEnabled = true );

    void Shutdown();
}
#endif
