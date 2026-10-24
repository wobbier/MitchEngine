#include "Editor/UIAutomation.h"

#include "Dementia.h"
#include "Engine/Engine.h"
#include "Cores/UI/RmlUiCore.h"

#if USING( ME_RMLUI )
#include <RmlUi/Core.h>
#endif

namespace
{
    std::string Trimmed( const std::string& InText )
    {
        const size_t first = InText.find_first_not_of( " \t\r\n" );
        if( first == std::string::npos )
        {
            return {};
        }
        const size_t last = InText.find_last_not_of( " \t\r\n" );
        return InText.substr( first, last - first + 1 );
    }
}

namespace UIAutomation
{
    bool Execute( const std::string& InCommand, const std::string& InArgs, std::string& OutError )
    {
#if USING( ME_RMLUI )
        RmlUiCore* core = GetEngine().RmlUi;
        Rml::Context* context = core != nullptr ? core->GetContext() : nullptr;
        if( context == nullptr )
        {
            OutError = "No RmlUi context exists yet";
            return false;
        }

        if( InCommand == "ui-click" )
        {
            const std::string selector = Trimmed( InArgs );
            Rml::Element* element = context->GetRootElement()->QuerySelector( selector );
            if( element == nullptr )
            {
                OutError = "No element matches '" + selector + "'";
                return false;
            }
            element->Focus();
            element->DispatchEvent( Rml::EventId::Click, Rml::Dictionary() );
            return true;
        }

        if( InCommand == "ui-assert-text" )
        {
            const size_t bar = InArgs.find( '|' );
            if( bar == std::string::npos )
            {
                OutError = "Usage: ui-assert-text <selector> | <expected>";
                return false;
            }
            const std::string selector = Trimmed( InArgs.substr( 0, bar ) );
            const std::string expected = Trimmed( InArgs.substr( bar + 1 ) );
            Rml::Element* element = context->GetRootElement()->QuerySelector( selector );
            if( element == nullptr )
            {
                OutError = "No element matches '" + selector + "'";
                return false;
            }
            const std::string actual = Trimmed( element->GetInnerRML() );
            if( actual != expected )
            {
                OutError = "'" + selector + "' reads '" + actual + "', expected '" + expected + "'";
                return false;
            }
            return true;
        }

        OutError = "Unknown UI command: " + InCommand;
        return false;
#else
        OutError = "RmlUi is not built";
        return false;
#endif
    }
}
