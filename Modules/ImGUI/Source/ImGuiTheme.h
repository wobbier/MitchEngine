#pragma once

#include <imgui.h>
#include <algorithm>
#include <cmath>

// Shared editor styling. Keep semantic/status colors independent of the chosen accent.
namespace ImGuiTheme
{
    enum class Mode { Dark, Light };

    inline ImVec4 DefaultAccent() { return ImVec4( 0.0f, 0.439f, 0.878f, 1.f ); }

    inline ImVec4 Mix( const ImVec4& a, const ImVec4& b, float amount )
    {
        return ImVec4( a.x + ( b.x - a.x ) * amount, a.y + ( b.y - a.y ) * amount,
            a.z + ( b.z - a.z ) * amount, a.w + ( b.w - a.w ) * amount );
    }

    inline ImVec4 WithAlpha( ImVec4 color, float alpha )
    {
        color.w = alpha;
        return color;
    }

    inline ImVec4 SanitizeAccent( ImVec4 color )
    {
        const ImVec4 fallback = DefaultAccent();
        auto channel = []( float value, float defaultValue ) {
            return std::isfinite( value ) ? std::clamp( value, 0.f, 1.f ) : defaultValue;
        };
        color.x = channel( color.x, fallback.x );
        color.y = channel( color.y, fallback.y );
        color.z = channel( color.z, fallback.z );
        color.w = 1.f;
        return color;
    }

    inline float Luminance( const ImVec4& color )
    {
        auto linear = []( float c ) { return c <= 0.04045f ? c / 12.92f : std::pow( ( c + 0.055f ) / 1.055f, 2.4f ); };
        return 0.2126f * linear( color.x ) + 0.7152f * linear( color.y ) + 0.0722f * linear( color.z );
    }

    inline float Contrast( const ImVec4& a, const ImVec4& b )
    {
        const float x = Luminance( a ), y = Luminance( b );
        return ( std::max( x, y ) + 0.05f ) / ( std::min( x, y ) + 0.05f );
    }

    // Preserve the chosen color for swatches; adjust small marks and text for readability.
    inline ImVec4 ReadableAccent( ImVec4 accent, const ImVec4& background, const ImVec4& text )
    {
        for( int i = 0; i < 24 && Contrast( accent, background ) < 4.5f; ++i )
            accent = Mix( accent, text, 0.12f );
        return accent;
    }

    inline ImVec4 Gray( float value, float alpha = 1.f )
    {
        return ImVec4( value, value, value, alpha );
    }

    // An accent filled behind text: pushed away from the text color until the label stays readable.
    inline ImVec4 AccentSurface( ImVec4 accent, const ImVec4& text )
    {
        const ImVec4 away = Luminance( text ) > 0.5f ? Gray( 0.f ) : Gray( 1.f );
        for( int i = 0; i < 24 && Contrast( accent, text ) < 4.5f; ++i )
            accent = Mix( accent, away, 0.08f );
        return accent;
    }

    struct Palette
    {
        ImVec4 Accent, Hovered, Pressed, AccentText;
        ImVec4 Text, Muted, Canvas, Window, Recessed, Header, Popup;
        ImVec4 Control, ControlHovered, ControlPressed, Border, Separator, Highlight;
        // Status text: fixed hues, adjusted until readable on Window in either theme.
        ImVec4 Info, Success, Warning, Error;
    };

    // Dark values are the editor's original palette (formerly UI/Colors.h): near-black chrome,
    // charcoal panels, lighter controls, and the accent filling hovered/pressed/active surfaces.
    inline Palette MakePalette( Mode mode, ImVec4 accent )
    {
        Palette p;
        const bool dark = mode == Mode::Dark;
        p.Text = Gray( dark ? 0.94f : 0.12f );
        p.Muted = Gray( dark ? 0.56f : 0.42f );
        p.Canvas = Gray( dark ? 0.059f : 0.80f );
        p.Window = Gray( dark ? 0.141f : 0.93f );
        p.Recessed = Gray( dark ? 0.102f : 0.88f );
        p.Header = Gray( dark ? 0.184f : 0.86f );
        p.Popup = Gray( dark ? 0.118f : 0.97f );
        p.Control = Gray( dark ? 0.22f : 0.985f );
        p.ControlHovered = Gray( dark ? 0.27f : 0.95f );
        p.ControlPressed = Gray( dark ? 0.18f : 0.90f );
        p.Border = Gray( dark ? 0.059f : 0.72f );
        // Dividers need their own contrast against panels and popups; chrome borders are too subtle.
        p.Separator = Gray( dark ? 0.36f : 0.65f );
        p.Highlight = Gray( dark ? 0.30f : 0.80f );

        const ImVec4 chosen = SanitizeAccent( accent );
        // Hover is the brightest readable shade; resting and pressed step away from it so the
        // three states stay distinct even after the contrast clamp.
        const ImVec4 away = dark ? Gray( 0.f ) : Gray( 1.f );
        p.Hovered = AccentSurface( Mix( chosen, Gray( 1.f ), dark ? 0.12f : 0.f ), p.Text );
        p.Accent = Mix( p.Hovered, away, 0.16f );
        p.Pressed = Mix( p.Hovered, away, 0.34f );
        p.AccentText = ReadableAccent( chosen, p.Window, p.Text );
        p.Info = ReadableAccent( ImVec4( 0.45f, 0.75f, 1.00f, 1.f ), p.Window, p.Text );
        p.Success = ReadableAccent( ImVec4( 0.40f, 0.85f, 0.45f, 1.f ), p.Window, p.Text );
        p.Warning = ReadableAccent( ImVec4( 1.00f, 0.80f, 0.30f, 1.f ), p.Window, p.Text );
        p.Error = ReadableAccent( ImVec4( 1.00f, 0.40f, 0.38f, 1.f ), p.Window, p.Text );
        return p;
    }

    // The palette the last Apply() installed: colors ImGui has no slot for (accent text,
    // status) are read from here.
    inline Palette& ActivePalette()
    {
        static Palette palette = MakePalette( Mode::Dark, DefaultAccent() );
        return palette;
    }

    // Accent-colored text (prefab instances, overridden values, active states).
    inline ImVec4 AccentText() { return ActivePalette().AccentText; }
    inline ImVec4 InfoText() { return ActivePalette().Info; }
    inline ImVec4 SuccessText() { return ActivePalette().Success; }
    inline ImVec4 WarningText() { return ActivePalette().Warning; }
    inline ImVec4 ErrorText() { return ActivePalette().Error; }

    // Vector component markers: X red, Y green, Z blue, W gray (the gizmo convention).
    inline ImU32 AxisColor( int axis )
    {
        static const ImU32 colors[4] = { IM_COL32( 230, 70, 80, 255 ), IM_COL32( 120, 200, 40, 255 ), IM_COL32( 50, 140, 250, 255 ), IM_COL32( 150, 150, 150, 255 ) };
        return colors[std::clamp( axis, 0, 3 )];
    }

    // ---- Layout ------------------------------------------------------------------------
    // Call sites never hardcode pixels: sizes come from the style or from the font, so the
    // whole editor keeps its proportions at every Appearance.FontSize.

    // The style as the last Apply() left it, unaffected by PushStyleVar (panels that zero
    // their padding or spacing still get the standard gaps from these tokens).
    inline ImGuiStyle& BaseStyle()
    {
        static ImGuiStyle style;
        return style;
    }

    // The font size the style metrics in Apply() are authored at.
    constexpr float kBaseFontSize = 18.f;

    // A length in ems (multiples of the current font size), rounded to whole pixels: column
    // widths, popup and list sizes, fields that sit outside a property row.
    inline float Em( float count ) { return std::round( ImGui::GetFontSize() * count ); }

    // Gap between the parts of one compound control: a field and its browse/clear buttons,
    // vector components, a swatch and its caption. Separate controls use plain SameLine().
    inline float InnerGap() { return ImGui::GetStyle().ItemInnerSpacing.x; }

    // A square button as tall as a field (browse "...", clear "x", add "+", remove "-").
    inline ImVec2 IconButtonSize() { return ImVec2( ImGui::GetFrameHeight(), ImGui::GetFrameHeight() ); }

    // Room taken by `count` icon buttons trailing a field, gaps included.
    inline float IconButtonsWidth( int count ) { return count * ( ImGui::GetFrameHeight() + InnerGap() ); }

    // Width of a button sized to its label (for fields that share a row with one).
    inline float ButtonWidth( const char* label )
    {
        return ImGui::CalcTextSize( label, nullptr, true ).x + ImGui::GetStyle().FramePadding.x * 2.f;
    }

    // Buttons in a row at the bottom of a dialog (Save / Cancel), all the same width.
    inline ImVec2 DialogButtonSize() { return ImVec2( Em( 6.5f ), 0.f ); }

    // Property rows (inspectors, settings): the label column's share of the row, up to
    // kMaxLabelColumn ems so values stay near their labels in wide windows.
    constexpr float kLabelColumn = 0.4f;
    constexpr float kMaxLabelColumn = 14.f;

    // Content inset of panels that zero WindowPadding so headers and lists run edge to edge.
    inline float PanelInset() { return BaseStyle().WindowPadding.x; }

    // Vertical gap that separates stacked groups (component sections, form blocks): twice the
    // item spacing, also where the spacing is pushed to zero.
    inline void SectionGap()
    {
        const float gap = BaseStyle().ItemSpacing.y * 2.f - ImGui::GetStyle().ItemSpacing.y;
        ImGui::Dummy( ImVec2( 0.f, std::max( gap, 0.f ) ) );
    }

    inline void Apply( Mode mode, ImVec4 accent, float fontSize = kBaseFontSize )
    {
        const Palette p = MakePalette( mode, accent );
        ActivePalette() = p;
        const bool dark = mode == Mode::Dark;
        ImGuiStyle& style = ImGui::GetStyle();
        // In ImGuiCol_ order. Keep the count checked so new ImGui slots need a deliberate color.
        const ImVec4 colors[] = {
            p.Text,                                                     // Text
            p.Muted,                                                    // TextDisabled
            p.Window,                                                   // WindowBg
            Gray( 0.f, 0.f ),                                           // ChildBg
            p.Popup,                                                    // PopupBg
            p.Border,                                                   // Border
            Gray( 0.f, 0.f ),                                           // BorderShadow
            p.Control,                                                  // FrameBg
            p.ControlHovered,                                           // FrameBgHovered
            p.ControlPressed,                                           // FrameBgActive
            p.Canvas,                                                   // TitleBg
            p.Canvas,                                                   // TitleBgActive
            p.Canvas,                                                   // TitleBgCollapsed
            p.Canvas,                                                   // MenuBarBg
            p.Window,                                                   // ScrollbarBg
            Mix( p.Window, p.Text, 0.22f ),                             // ScrollbarGrab
            Mix( p.Window, p.Text, 0.32f ),                             // ScrollbarGrabHovered
            Mix( p.Window, p.Text, 0.42f ),                             // ScrollbarGrabActive
            p.Text,                                                     // CheckMark
            Mix( p.Control, p.Text, 0.40f ),                            // SliderGrab
            p.AccentText,                                               // SliderGrabActive
            p.Control,                                                  // Button
            p.Hovered,                                                  // ButtonHovered
            p.Pressed,                                                  // ButtonActive
            p.Highlight,                                                // Header
            p.Hovered,                                                  // HeaderHovered
            p.Pressed,                                                  // HeaderActive
            p.Separator,                                                // Separator
            p.Hovered,                                                  // SeparatorHovered
            p.Pressed,                                                  // SeparatorActive
            WithAlpha( p.Muted, 0.25f ),                                // ResizeGrip
            p.Hovered,                                                  // ResizeGripHovered
            p.Pressed,                                                  // ResizeGripActive
            p.Recessed,                                                 // Tab
            p.Hovered,                                                  // TabHovered
            p.Accent,                                                   // TabActive
            p.Recessed,                                                 // TabUnfocused
            p.Window,                                                   // TabUnfocusedActive
            WithAlpha( p.Accent, 0.40f ),                               // DockingPreview
            p.Canvas,                                                   // DockingEmptyBg
            p.Muted,                                                    // PlotLines
            p.AccentText,                                               // PlotLinesHovered
            ImVec4( 0.73f, 0.60f, 0.15f, 1.f ),                         // PlotHistogram
            ImVec4( 1.00f, 0.60f, 0.00f, 1.f ),                         // PlotHistogramHovered
            p.Header,                                                   // TableHeaderBg
            p.Border,                                                   // TableBorderStrong
            Mix( p.Window, p.Border, 0.50f ),                           // TableBorderLight
            Gray( 0.f, 0.f ),                                           // TableRowBg
            WithAlpha( p.Text, 0.03f ),                                 // TableRowBgAlt
            WithAlpha( p.Accent, 0.40f ),                               // TextSelectedBg
            p.AccentText,                                               // DragDropTarget
            p.AccentText,                                               // NavHighlight
            WithAlpha( p.Text, 0.70f ),                                 // NavWindowingHighlight
            Gray( 0.f, 0.20f ),                                         // NavWindowingDimBg
            Gray( 0.f, dark ? 0.45f : 0.25f ),                          // ModalWindowDimBg
        };
        static_assert( IM_ARRAYSIZE( colors ) == ImGuiCol_COUNT, "Define a theme color for every ImGui slot" );
        std::copy_n( colors, ImGuiCol_COUNT, style.Colors );

        // Original editor proportions, with a little more room around the larger (18px+) text.
        // Authored at kBaseFontSize and scaled with the font, so spacing keeps its proportions
        // at every size; whole pixels keep frame edges crisp. Border widths stay fixed.
        const float scale = std::isfinite( fontSize ) && fontSize > 0.f ? fontSize / kBaseFontSize : 1.f;
        auto px = [scale]( float value ) { return std::round( value * scale ); };
        style.WindowPadding = ImVec2( px( 8 ), px( 8 ) );
        style.FramePadding = ImVec2( px( 6 ), px( 4 ) );
        style.ItemSpacing = ImVec2( px( 8 ), px( 5 ) );
        style.ItemInnerSpacing = ImVec2( px( 6 ), px( 4 ) );
        style.CellPadding = ImVec2( px( 6 ), px( 3 ) );
        style.TouchExtraPadding = ImVec2( 0, 0 );
        style.IndentSpacing = px( 22 );
        style.ColumnsMinSpacing = px( 6 );
        style.WindowMinSize = ImVec2( px( 50 ), px( 50 ) );
        style.WindowRounding = ( ImGui::GetIO().ConfigFlags & ImGuiConfigFlags_ViewportsEnable ) ? 0.f : px( 3 );
        style.ChildRounding = px( 2 );
        style.PopupRounding = px( 3 );
        style.FrameRounding = px( 2 );
        style.GrabRounding = px( 2 );
        style.TabRounding = px( 2 );
        style.ScrollbarRounding = px( 6 );
        style.WindowBorderSize = 0.f;
        style.ChildBorderSize = 0.f;
        style.PopupBorderSize = 1.f;
        style.FrameBorderSize = 0.f;
        style.TabBorderSize = 0.f;
        style.TabBarBorderSize = px( 2 );
        style.ScrollbarSize = px( 14 );
        style.GrabMinSize = px( 12 );
        style.LogSliderDeadzone = px( 4 );
        style.DockingSeparatorSize = px( 6 );
        style.SeparatorTextBorderSize = 1.f;
        style.SeparatorTextAlign = ImVec2( 0.f, 0.5f );
        // Leave a short leading rule as well as the trailing rule on every labeled divider.
        style.SeparatorTextPadding = ImVec2( px( 20 ), px( 4 ) );
        style.DisplayWindowPadding = ImVec2( px( 19 ), px( 19 ) );
        style.DisplaySafeAreaPadding = ImVec2( px( 3 ), px( 3 ) );
        style.MouseCursorScale = 1.f;
        style.WindowTitleAlign = ImVec2( 0.5f, 0.5f );
        style.WindowMenuButtonPosition = ImGuiDir_None;
        style.ColorButtonPosition = ImGuiDir_Left;
        style.DisabledAlpha = 0.50f;
        style.AntiAliasedFill = true;
        style.AntiAliasedLines = true;
        BaseStyle() = style;
    }
}
