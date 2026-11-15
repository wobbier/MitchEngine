#pragma once
#include "Dementia.h"
#include <functional>
#include <string>
#include <utility>
#include <vector>

#if USING( ME_EDITOR )

// Editor automation commands a game (or any code outside Havana) adds to --editor-exec scripts:
// every command starting with a registered prefix ("track-") goes to its handler, which answers
// false with an error to fail the script line. See Docs/Editor-Havana.md, Automation.
namespace AutomationCommands
{
    using Handler = std::function<bool( const std::string& InCommand, const std::string& InArgs, std::string& OutError )>;

    inline std::vector<std::pair<std::string, Handler>>& Registry()
    {
        static std::vector<std::pair<std::string, Handler>> registry;
        return registry;
    }

    // Replaces the handler of a prefix registered before.
    inline void Register( const std::string& InPrefix, Handler InHandler )
    {
        for( auto& entry : Registry() )
        {
            if( entry.first == InPrefix )
            {
                entry.second = std::move( InHandler );
                return;
            }
        }
        Registry().emplace_back( InPrefix, std::move( InHandler ) );
    }

    inline void Unregister( const std::string& InPrefix )
    {
        auto& registry = Registry();
        for( auto it = registry.begin(); it != registry.end(); ++it )
        {
            if( it->first == InPrefix )
            {
                registry.erase( it );
                return;
            }
        }
    }

    inline const Handler* Find( const std::string& InCommand )
    {
        for( const auto& entry : Registry() )
        {
            if( InCommand.rfind( entry.first, 0 ) == 0 )
            {
                return &entry.second;
            }
        }
        return nullptr;
    }
}

#endif
