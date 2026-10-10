#pragma once
#include "JSON.h"
#include <cstdint>
#include <future>
#include <string>
#include <vector>

class World;

// Scenes loaded on top of the main one: level chunks streamed in and out, a shared lighting or
// gameplay scene, menus over a level. Their entities join the world tagged with the scene's id
// (World::SetLoadingScene), so a scene unloads as a unit and never saves into the main scene. The
// main scene changing (its world unloads) forgets them all. Engine owns one: LoadSceneAdditive.
class SceneStreaming
{
public:
    enum class State : uint8_t
    {
        None = 0,   // no such scene (never loaded, or unloaded)
        Loading,    // being read and parsed on a worker thread
        Loaded,
        Failed,     // the file couldn't be read or parsed
    };

    ~SceneStreaming();

    // Reads, parses and instantiates the scene now. Returns its id (0 when it can't be loaded).
    uint16_t Load( World& InWorld, const std::string& InPath );
    // Reads and parses on a worker thread; Pump instantiates it once that's done. Returns its id at
    // once (State::Loading until then).
    uint16_t LoadAsync( const std::string& InPath );
    // Main thread, each frame: instantiates the async scenes whose data is ready, in request order.
    // Returns their ids.
    std::vector<uint16_t> Pump( World& InWorld );
    // Main thread, before a frame that must not change with load timing: finishes every async load.
    std::vector<uint16_t> Wait( World& InWorld );
    // Destroys the scene's entities (deferred to the next sync point) and forgets it. A scene still
    // loading is cancelled. False for unknown ids.
    bool Unload( World& InWorld, uint16_t InId );
    // The main scene changed and its world dropped every entity: forget the additive scenes.
    void Reset();

    State GetState( uint16_t InId ) const;
    std::string GetPath( uint16_t InId ) const;
    // Loaded scenes, oldest first.
    std::vector<uint16_t> GetLoaded() const;

private:
    struct Entry
    {
        uint16_t Id = 0;
        std::string Path;
        State Status = State::None;
        std::future<json> Pending;
    };
    std::vector<Entry> m_scenes;
    uint16_t m_nextId = 0;

    uint16_t NextId();
    Entry* Find( uint16_t InId );
    const Entry* Find( uint16_t InId ) const;
    // Reads, parses and migrates the scene (any thread). Null on failure.
    static json ReadScene( const std::string& InPath );
    void Instantiate( World& InWorld, Entry& InEntry, json InData );
};
