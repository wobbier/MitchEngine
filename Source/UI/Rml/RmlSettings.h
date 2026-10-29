#pragma once
#include "Dementia.h"

#if USING( ME_RMLUI )

#include <string>

namespace Rml
{
    class Element;
    class ElementDocument;
    class Event;
}

// Player settings in RmlUi documents (PLAY-3): what the stock options menu
// (Engine/Assets/UI/Options/Options.rml) is made of, usable by any document.
//
// Inline actions (onclick="..."):
//   setting:Audio.Music|0.5               set a PlayerSettings key (JSON, or else a string: |Low)
//   setting-step:Audio.Music|0.1          add to a number (clamped to its range)
//   setting-toggle:Accessibility.ReduceMotion
//   rebind:Jump|0                         "press a key" for binding 0 of Jump (|0|Up for a part)
//   reset-bindings, reset-settings
//   window-mode:windowed|borderless|fullscreen
//   tab:page-audio                        show #page-audio, hide the other .tab-page elements,
//                                         and mark the clicked .tab selected
// Attributes, kept current whenever the settings change:
//   data-setting-text="Audio.Music"       the value as text; data-format="percent", "scale" or
//                                         "onoff", data-empty="Default" for an empty string
//   data-setting-selected="Graphics.Quality|Low"   class "selected" while the key has that value
//   data-window-mode="borderless"         class "selected" in that window mode
//   data-binding="Jump|0|Up"              the binding's control ("Press a key..." while rebinding)
// Generated once per document load:
//   data-options="quality"                a choice button per quality preset
//   data-options="languages"              Default plus one per Assets/UI/Strings/<code>.json
//   data-controls                         a row per action of the game input's enabled-by-default
//                                         contexts, a button per binding (or composite part)
namespace RmlSettings
{
    // True when InAction was one of the actions above.
    bool HandleAction( const std::string& InAction, const std::string& InArgs, Rml::Event& InEvent );

    // Fills a freshly loaded document's generated lists, then refreshes it.
    void OnDocumentLoaded( Rml::ElementDocument& InDocument );
    void Refresh( Rml::ElementDocument& InDocument );
    // Once a frame: true when the settings or a rebind changed since the last call.
    bool ConsumeChanged();

    // While a rebind waits for a control, and on the frame it ends, the UI gets no keys, buttons
    // or gamepad navigation: the press that binds (or cancels) must not also click the menu.
    bool HoldsInput();
}

#endif
