#include "PCH.h"
#include "UI/Rml/RmlSettings.h"

#if USING( ME_RMLUI )

#include "CLog.h"
#include "Engine/Engine.h"
#include "Engine/HardwareBudget.h"
#include "Engine/Input.h"
#include "Engine/Localization.h"
#include "Engine/PlayerSettings.h"
#include "Input/InputActions.h"
#include "JSON.h"
#include "VFS/VFS.h"
#include "Window/IWindow.h"
#include <RmlUi/Core.h>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <functional>
#include <vector>

namespace
{
    // The binding a "press a key" rebind is waiting on ("Jump|0|Up"), empty when none.
    std::string s_rebinding;
    bool s_wasRebinding = false;
    bool s_changed = true;
    uint64_t s_seenRevision = ~0ull;
    WindowMode s_seenWindowMode = WindowMode::Windowed;

    // Rebinds wait for these instead (Escape on the keyboard, Start on a pad).
    const std::vector<std::string> kCancelControls = { "Keyboard/Escape", "Gamepad/Start" };


    InputActionSystem& Actions()
    {
        return GetEngine().GetInput().GetActions();
    }


    std::vector<std::string> Split( const std::string& InText, char InSeparator )
    {
        std::vector<std::string> parts;
        size_t start = 0;
        while( true )
        {
            const size_t at = InText.find( InSeparator, start );
            parts.push_back( InText.substr( start, at == std::string::npos ? std::string::npos : at - start ) );
            if( at == std::string::npos )
            {
                return parts;
            }
            start = at + 1;
        }
    }


    std::string Escape( const std::string& InText )
    {
        std::string out;
        for( const char c : InText )
        {
            switch( c )
            {
            case '&': out += "&amp;"; break;
            case '<': out += "&lt;"; break;
            case '>': out += "&gt;"; break;
            case '"': out += "&quot;"; break;
            default: out += c; break;
            }
        }
        return out;
    }


    // An id-safe spelling: "Move Left" -> "Move_Left".
    std::string IdPart( const std::string& InText )
    {
        std::string out;
        for( const char c : InText )
        {
            const bool safe = ( c >= 'a' && c <= 'z' ) || ( c >= 'A' && c <= 'Z' ) || ( c >= '0' && c <= '9' ) || c == '-';
            out += safe ? c : '_';
        }
        return out;
    }


    // "Low" is a string, "0.5" a number, "true" a bool, nothing at all an empty string.
    json ParseValue( const std::string& InText )
    {
        const json value = json::parse( InText, nullptr, false );
        return value.is_discarded() ? json( InText ) : value;
    }


    void SetSetting( const std::string& InKey, const json& InValue )
    {
        std::string error;
        if( !PlayerSettings::Get().SetValue( InKey, InValue, &error ) )
        {
            YIKES( "[UI] " + error );
        }
    }


    std::string Format( const json& InValue, const std::string& InFormat, const std::string& InEmpty )
    {
        if( InValue.is_boolean() )
        {
            const bool on = InValue.get<bool>();
            return Localization::Get().Get( on ? "On" : "Off" );
        }
        if( InValue.is_number() )
        {
            const double number = InValue.get<double>();
            char text[32];
            if( InFormat == "percent" )
            {
                std::snprintf( text, sizeof( text ), "%d%%", static_cast<int>( std::lround( number * 100.0 ) ) );
            }
            else if( InFormat == "scale" )
            {
                std::snprintf( text, sizeof( text ), "%.2gx", number );
            }
            else
            {
                std::snprintf( text, sizeof( text ), "%g", number );
            }
            Localization::Get().MarkData( text );
            return text;
        }
        if( InValue.is_string() && !InValue.get<std::string>().empty() )
        {
            return Localization::Get().Get( InValue.get<std::string>() );
        }
        return InEmpty.empty() ? InEmpty : Localization::Get().Get( InEmpty );
    }


    // "Jump|0|Up" -> the control's display ("Press a key..." while it's being rebound).
    std::string BindingText( const std::string& InBinding, const std::string& InLabel )
    {
        const std::vector<std::string> parts = Split( InBinding, '|' );
        const size_t index = parts.size() > 1 ? static_cast<size_t>( std::max( 0, std::atoi( parts[1].c_str() ) ) ) : 0;
        const std::string part = parts.size() > 2 ? parts[2] : std::string();
        // Key names are data; the prompt and the part labels translate.
        std::string control = Actions().GetBindingPartDisplay( parts[0], index, part );
        Localization::Get().MarkData( control );
        if( InBinding == s_rebinding )
        {
            control = Localization::Get().Get( "Press a key..." );
        }
        const std::string text = InLabel.empty() ? control : Localization::Get().Get( InLabel ) + ": " + control;
        Localization::Get().MarkData( text );
        return text;
    }


    void ForEachElement( Rml::Element& InRoot, const std::function<void( Rml::Element& )>& InVisit )
    {
        InVisit( InRoot );
        for( int i = 0; i < InRoot.GetNumChildren(); ++i )
        {
            ForEachElement( *InRoot.GetChild( i ), InVisit );
        }
    }


    std::string Attribute( Rml::Element& InElement, const char* InName )
    {
        return InElement.GetAttribute<Rml::String>( InName, "" );
    }


    // Only touches the element when the text changes: rewriting it would restart transitions.
    // The text is already translated (Localization::Get) or data, so RmlUi's pass over it at parse
    // mustn't report it missing.
    void SetText( Rml::Element& InElement, const std::string& InText )
    {
        Localization::Get().MarkData( InText );
        const std::string escaped = Escape( InText );
        if( InElement.GetInnerRML() != escaped )
        {
            InElement.SetInnerRML( escaped );
        }
    }


    std::string ChoiceButton( const std::string& InId, const std::string& InKey, const std::string& InValue, const std::string& InLabel )
    {
        return "<button class=\"choice\" id=\"" + InId + "\" onclick=\"setting:" + Escape( InKey + "|" + InValue ) + "\" data-setting-selected=\""
            + Escape( InKey + "|" + InValue ) + "\">" + Escape( InLabel ) + "</button>";
    }


    std::string QualityChoices()
    {
        std::string rml;
        for( const QualityPreset& preset : HardwareBudget::Get().GetQualityPresets() )
        {
            rml += ChoiceButton( "quality-" + IdPart( preset.Name ), "Graphics.Quality", preset.Name, preset.Name );
        }
        return rml;
    }


    std::string LanguageChoices()
    {
        // "" (nothing after the bar) keeps the documents' own text. Each language shows its own
        // name ("language.name"), which is data in every other language.
        std::string rml = ChoiceButton( "language-default", "Language", "", "Default" );
        for( const std::string& code : Localization::Get().GetLanguages() )
        {
            const std::string label = Localization::Get().GetLanguageName( code );
            Localization::Get().MarkData( label );
            rml += ChoiceButton( "language-" + IdPart( code ), "Language", code, label );
        }
        return rml;
    }


    std::string BindingButton( const std::string& InAction, size_t InIndex, const std::string& InPart )
    {
        const std::string binding = InAction + "|" + std::to_string( InIndex ) + ( InPart.empty() ? std::string() : "|" + InPart );
        const std::string id = "bind-" + IdPart( InAction ) + "-" + std::to_string( InIndex ) + ( InPart.empty() ? std::string() : "-" + IdPart( InPart ) );
        return "<button class=\"bind\" id=\"" + id + "\" onclick=\"rebind:" + Escape( binding ) + "\" data-binding=\"" + Escape( binding ) + "\" data-binding-label=\""
            + Escape( InPart ) + "\">" + Escape( BindingText( binding, InPart ) ) + "</button>";
    }


    std::string ControlRows()
    {
        std::string rml;
        // The contexts play starts with: a disabled-by-default one (a UI "Menu" map) isn't the
        // player's controls.
        for( const InputContext& context : Actions().GetMap().Contexts )
        {
            if( !context.EnabledByDefault )
            {
                continue;
            }
            for( const InputAction& action : context.Actions )
            {
                rml += "<div class=\"control-row\"><span class=\"control-name\">" + Escape( action.Name ) + "</span><div class=\"control-bindings\">";
                for( size_t index = 0; index < action.Bindings.size(); ++index )
                {
                    switch( action.Bindings[index].Kind )
                    {
                    case InputBindingKind::Vector2:
                        for( const char* part : { "Up", "Left", "Down", "Right" } )
                        {
                            rml += BindingButton( action.Name, index, part );
                        }
                        break;
                    case InputBindingKind::Axis:
                        rml += BindingButton( action.Name, index, "Negative" );
                        rml += BindingButton( action.Name, index, "Positive" );
                        break;
                    case InputBindingKind::Control:
                    default:
                        rml += BindingButton( action.Name, index, "" );
                        break;
                    }
                }
                rml += "</div></div>";
            }
        }
        return rml;
    }


    void StartRebind( const std::string& InBinding )
    {
        const std::vector<std::string> parts = Split( InBinding, '|' );
        if( parts[0].empty() || !Actions().HasAction( parts[0] ) )
        {
            YIKES( "[UI] rebind: no action named '" + parts[0] + "'" );
            return;
        }
        const size_t index = parts.size() > 1 ? static_cast<size_t>( std::max( 0, std::atoi( parts[1].c_str() ) ) ) : 0;
        const std::string part = parts.size() > 2 ? parts[2] : std::string();
        s_rebinding = InBinding;
        s_changed = true;
        Actions().StartInteractiveRebind( parts[0], index, part, [InBinding]( const std::string& InControl ) {
            s_rebinding.clear();
            s_changed = true;
            if( InControl.empty() )
            {
                INFO( "UI", "UI: rebind " + InBinding + " cancelled" );
                return;
            }
            PlayerSettings::Get().CaptureBindings();
            INFO( "UI", "UI: rebound " + InBinding + " to " + InControl );
        }, kCancelControls );
        INFO( "UI", "UI: rebind " + InBinding + ": press a key" );
    }


    void ShowTab( const std::string& InPage, Rml::Event& InEvent )
    {
        Rml::Element* clicked = InEvent.GetCurrentElement();
        Rml::ElementDocument* document = clicked ? clicked->GetOwnerDocument() : nullptr;
        if( !document )
        {
            return;
        }
        ForEachElement( *document, [&]( Rml::Element& InElement ) {
            if( InElement.IsClassSet( "tab-page" ) )
            {
                if( InElement.GetId() == InPage )
                {
                    InElement.RemoveProperty( "display" );
                }
                else
                {
                    InElement.SetProperty( "display", "none" );
                }
            }
            else if( InElement.IsClassSet( "tab" ) )
            {
                InElement.SetClass( "selected", &InElement == clicked );
            }
        } );
    }
}


bool RmlSettings::HandleAction( const std::string& InAction, const std::string& InArgs, Rml::Event& InEvent )
{
    const size_t bar = InArgs.find( '|' );
    const std::string key = InArgs.substr( 0, bar );
    const std::string value = bar == std::string::npos ? std::string() : InArgs.substr( bar + 1 );
    if( InAction == "setting" )
    {
        SetSetting( key, ParseValue( value ) );
    }
    else if( InAction == "setting-step" )
    {
        const json current = PlayerSettings::Get().GetValue( key );
        if( !current.is_number() )
        {
            YIKES( "[UI] setting-step: " + key + " isn't a number" );
            return true;
        }
        // Rounded so ten steps of 0.1 land on 1 rather than 0.9999999.
        const double stepped = std::round( ( current.get<double>() + std::atof( value.c_str() ) ) * 1000.0 ) / 1000.0;
        SetSetting( key, stepped );
    }
    else if( InAction == "setting-toggle" )
    {
        const json current = PlayerSettings::Get().GetValue( key );
        SetSetting( key, !( current.is_boolean() && current.get<bool>() ) );
    }
    else if( InAction == "rebind" )
    {
        if( !Actions().IsRebinding() )
        {
            StartRebind( InArgs );
        }
    }
    else if( InAction == "reset-bindings" )
    {
        PlayerSettings::Get().ResetBindings();
    }
    else if( InAction == "reset-settings" )
    {
        PlayerSettings::Get().Reset();
    }
    else if( InAction == "window-mode" )
    {
        WindowMode mode = WindowMode::Windowed;
        if( !ParseWindowMode( InArgs, mode ) )
        {
            YIKES( "[UI] window-mode: expected windowed, borderless or fullscreen, not '" + InArgs + "'" );
        }
#if USING( ME_EDITOR )
        else
        {
            INFO( "UI", std::string( "UI: window-mode " ) + ToString( mode ) + " (ignored in the editor)" );
        }
#else
        else if( IWindow* window = GetEngine().GetWindow() )
        {
            window->SetWindowMode( mode, mode == WindowMode::Windowed ? GetEngine().GetConfig().WindowSize : GetEngine().GetConfig().FullscreenResolution );
            s_changed = true;
        }
#endif
    }
    else if( InAction == "tab" )
    {
        ShowTab( InArgs, InEvent );
    }
    else
    {
        return false;
    }
    return true;
}


void RmlSettings::OnDocumentLoaded( Rml::ElementDocument& InDocument )
{
    ForEachElement( InDocument, []( Rml::Element& InElement ) {
        const std::string options = Attribute( InElement, "data-options" );
        if( options == "quality" )
        {
            InElement.SetInnerRML( QualityChoices() );
        }
        else if( options == "languages" )
        {
            InElement.SetInnerRML( LanguageChoices() );
        }
        else if( InElement.HasAttribute( "data-controls" ) )
        {
            InElement.SetInnerRML( ControlRows() );
        }
    } );
    Refresh( InDocument );
}


void RmlSettings::Refresh( Rml::ElementDocument& InDocument )
{
    const PlayerSettings& settings = PlayerSettings::Get();
    const IWindow* window = GetEngine().GetWindow();
    const WindowMode windowMode = window ? window->GetWindowMode() : WindowMode::Windowed;
    ForEachElement( InDocument, [&]( Rml::Element& InElement ) {
        if( InElement.HasAttribute( "data-setting-text" ) )
        {
            SetText( InElement, Format( settings.GetValue( Attribute( InElement, "data-setting-text" ) ), Attribute( InElement, "data-format" ), Attribute( InElement, "data-empty" ) ) );
        }
        if( InElement.HasAttribute( "data-setting-selected" ) )
        {
            const std::string selected = Attribute( InElement, "data-setting-selected" );
            const size_t bar = selected.find( '|' );
            const json expected = ParseValue( bar == std::string::npos ? std::string() : selected.substr( bar + 1 ) );
            InElement.SetClass( "selected", settings.GetValue( selected.substr( 0, bar ) ) == expected );
        }
        if( InElement.HasAttribute( "data-window-mode" ) )
        {
            WindowMode mode = WindowMode::Windowed;
            InElement.SetClass( "selected", ParseWindowMode( Attribute( InElement, "data-window-mode" ), mode ) && mode == windowMode );
        }
        if( InElement.HasAttribute( "data-binding" ) )
        {
            SetText( InElement, BindingText( Attribute( InElement, "data-binding" ), Attribute( InElement, "data-binding-label" ) ) );
            InElement.SetClass( "waiting", Attribute( InElement, "data-binding" ) == s_rebinding );
        }
    } );
}


bool RmlSettings::ConsumeChanged()
{
    const uint64_t revision = PlayerSettings::Get().GetRevision();
    const IWindow* window = GetEngine().GetWindow();
    const WindowMode windowMode = window ? window->GetWindowMode() : WindowMode::Windowed;
    // A rebind can end without the settings changing (cancelled): its button goes back too.
    if( !s_rebinding.empty() && !Actions().IsRebinding() )
    {
        s_rebinding.clear();
        s_changed = true;
    }
    const bool changed = s_changed || revision != s_seenRevision || windowMode != s_seenWindowMode;
    s_changed = false;
    s_seenRevision = revision;
    s_seenWindowMode = windowMode;
    return changed;
}


bool RmlSettings::HoldsInput()
{
    const bool rebinding = Actions().IsRebinding();
    const bool hold = rebinding || s_wasRebinding;
    s_wasRebinding = rebinding;
    return hold;
}

#endif
