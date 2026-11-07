#pragma once
#include "Dementia.h"
#if USING( ME_EDITOR )
#include <string>

// Editor automation that drives ImGui widgets the way a person does: real pointer and key input,
// aimed at items found by label (MeWidgets::ItemLog query syntax: "Light/Intensity",
// "Light/Intensity:label", "Transform/Position/Y", "Name@2", "0x1A2B3C4D"). Items come from the
// previous frame; one scrolled out of view is scrolled into view first. Every gesture spreads over
// several frames (hover, press, moves, release), so ImGui sees the same event order as a mouse.
//   imgui-click <item> [| right|double] [| shift|ctrl|alt ...]
//   imgui-drag <item> | <dx> <dy> [| shift|ctrl|alt ...]   press, move by (dx, dy) px, release
//   imgui-scrub <item> | <dx> [| shift|ctrl|alt ...]       a horizontal drag (labels, scrub fields)
//   imgui-type <item> | <text>      Ctrl+click into text entry, select all, type, Enter
//   imgui-hover <item>              moves the pointer over the item and leaves it there
//   imgui-assert-value <item> | <value> [~ tol]   the value the widget displays (numbers compare
//                                   within tol, 0.001 by default; text compares exactly, ignoring case)
//   imgui-assert-item <item> [| true|false]       the item was drawn last frame (or wasn't)
//   imgui-dump [<filter>]           logs every item drawn last frame (window, section, label, row)
//   panel-screenshot <window> | <path.png>   the main window's screenshot cropped to that panel
namespace ImGuiAutomation
{
    // Called once per automation tick before commands run.
    void Tick();
    bool Execute( const std::string& InCommand, const std::string& InArgs, std::string& OutError, bool& OutRetry );
    bool Handles( const std::string& InCommand );
}
#endif
