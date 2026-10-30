#include "PCH.h"
#include "Mods/ModResolver.h"
#include <algorithm>
#include <functional>
#include <map>


ModSettings ModSettings::FromJson( const json& InJson )
{
    ModSettings settings;
    if( !InJson.is_object() )
    {
        return settings;
    }
    if( InJson.contains( "Order" ) && InJson["Order"].is_array() )
    {
        for( const json& id : InJson["Order"] )
        {
            if( id.is_string() && std::find( settings.Order.begin(), settings.Order.end(), id.get<std::string>() ) == settings.Order.end() )
            {
                settings.Order.push_back( id.get<std::string>() );
            }
        }
    }
    if( InJson.contains( "Disabled" ) && InJson["Disabled"].is_array() )
    {
        for( const json& id : InJson["Disabled"] )
        {
            if( id.is_string() )
            {
                settings.Disabled.insert( id.get<std::string>() );
            }
        }
    }
    return settings;
}


json ModSettings::ToJson() const
{
    return json{ { "Order", Order }, { "Disabled", std::vector<std::string>( Disabled.begin(), Disabled.end() ) } };
}


ModResolution ModResolver::Resolve( const std::vector<ModManifest>& InDiscovered, const ModSettings& InSettings, const SemVer& InEngine )
{
    ModResolution result;
    std::map<std::string, const ModManifest*> byId;
    for( const ModManifest& manifest : InDiscovered )
    {
        if( !byId.emplace( manifest.Id, &manifest ).second )
        {
            result.Problems.push_back( manifest.Id + ": another mod with this id was found first (" + byId[manifest.Id]->Root + "); " + manifest.Root + " is ignored" );
        }
    }

    // Enabled and compatible with this engine.
    std::map<std::string, const ModManifest*> live;
    for( const auto& [id, manifest] : byId )
    {
        VersionRange engine;
        if( InSettings.Disabled.count( id ) )
        {
            continue;
        }
        if( !VersionRange::Parse( manifest->EngineRange, engine ) || !engine.Contains( InEngine ) )
        {
            result.Problems.push_back( id + ": needs engine " + manifest->EngineRange + ", this is " + InEngine.ToString() );
            continue;
        }
        live[id] = manifest;
    }

    // Dependencies, until nothing more drops (a dropped mod takes its dependents with it).
    for( bool dropped = true; dropped; )
    {
        dropped = false;
        for( auto it = live.begin(); it != live.end(); )
        {
            std::string problem;
            for( const auto& [dependency, rangeText] : it->second->Dependencies )
            {
                VersionRange range;
                VersionRange::Parse( rangeText, range );
                const auto found = live.find( dependency );
                if( found == live.end() )
                {
                    problem = byId.count( dependency ) ? "needs " + dependency + ", which is disabled or can't load" : "needs " + dependency + ", which isn't installed";
                    break;
                }
                if( !range.Contains( found->second->Version ) )
                {
                    problem = "needs " + dependency + " " + rangeText + ", found " + found->second->Version.ToString();
                    break;
                }
            }
            if( problem.empty() )
            {
                ++it;
                continue;
            }
            result.Problems.push_back( it->first + ": " + problem );
            it = live.erase( it );
            dropped = true;
        }
    }

    // The player's order: named mods first, then the rest by id.
    std::map<std::string, size_t> rank;
    for( const std::string& id : InSettings.Order )
    {
        if( live.count( id ) && !rank.count( id ) )
        {
            const size_t next = rank.size();
            rank[id] = next;
        }
    }
    for( const auto& [id, manifest] : live )
    {
        if( !rank.count( id ) )
        {
            const size_t next = rank.size();
            rank[id] = next;
        }
    }

    // Constraints: an edge A -> B means A loads before B.
    std::map<std::string, std::set<std::string>> after;     // node -> what must come after it
    std::map<std::string, int> incoming;
    for( const auto& [id, manifest] : live )
    {
        incoming[id] += 0;
        auto edge = [&]( const std::string& InFirst, const std::string& InSecond ) {
            if( live.count( InFirst ) && live.count( InSecond ) && InFirst != InSecond && after[InFirst].insert( InSecond ).second )
            {
                ++incoming[InSecond];
            }
        };
        for( const auto& [dependency, range] : manifest->Dependencies )
        {
            edge( dependency, id );
        }
        for( const std::string& other : manifest->LoadAfter )
        {
            edge( other, id );
        }
        for( const std::string& other : manifest->LoadBefore )
        {
            edge( id, other );
        }
    }

    // Kahn's algorithm, always taking the ready mod the player ranked first.
    auto byRank = [&rank]( const std::string& InA, const std::string& InB ) { return rank[InA] > rank[InB]; };
    std::vector<std::string> ready;
    for( const auto& [id, count] : incoming )
    {
        if( count == 0 )
        {
            ready.push_back( id );
        }
    }
    std::make_heap( ready.begin(), ready.end(), byRank );
    std::set<std::string> placed;
    while( !ready.empty() )
    {
        std::pop_heap( ready.begin(), ready.end(), byRank );
        const std::string id = ready.back();
        ready.pop_back();
        placed.insert( id );
        result.Load.push_back( live[id] );
        for( const std::string& next : after[id] )
        {
            if( --incoming[next] == 0 )
            {
                ready.push_back( next );
                std::push_heap( ready.begin(), ready.end(), byRank );
            }
        }
    }
    for( const auto& [id, manifest] : live )
    {
        if( !placed.count( id ) )
        {
            result.Problems.push_back( id + ": its load order is circular, or it needs a mod whose order is (dependencies or loadAfter / loadBefore loop back)" );
        }
    }
    return result;
}
