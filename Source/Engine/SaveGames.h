#pragma once
#include "Dementia.h"
#include "JSON.h"
#include <future>
#include <string>
#include <vector>

// Save games (PLAY-4): named slots under user://Saves, one folder each.
//
//   user://Saves/<slot>/Save.json       { "Version", "Meta": {...}, "World": <scene v2 JSON> }
//   user://Saves/<slot>/Meta.json       the Meta object alone, for listing slots cheaply
//   user://Saves/<slot>/Thumbnail.png   the frame the save was taken on
//
// The world is the scene serializer's snapshot of everything under the scene root, the way the
// editor's Play snapshot is taken: transforms, components, script fields (public ones plus
// non-public [Save] fields), entity references by GUID. Snapshots are taken on the main thread;
// the JSON is written on a worker, atomically (temporary files, then renames: Save.json first,
// Meta.json last). A load is queued and runs at the end of the frame: the current scene goes and
// the slot's world loads in its place (Engine::LoadSceneFromData, under the original scene path).
// Main thread only, apart from the write.
class SaveGames
{
public:
    static constexpr int kVersion = 1;
    static constexpr const char* kRoot = "user://Saves";

    struct SlotInfo
    {
        std::string Slot;
        std::string Name;
        std::string Scene;          // the scene the save was taken in
        std::string Date;           // UTC, ISO 8601
        double Playtime = 0.0;      // seconds of play, across sessions
        int Version = 0;
        json ToJson() const;
        static SlotInfo FromJson( const std::string& InSlot, const json& InMeta );
    };

    static SaveGames& Get();

    // Letters, digits, '-' and '_' (a slot is a folder name), at most 64 characters.
    static bool IsValidSlot( const std::string& InSlot );
    static std::string GetSlotDirectory( const std::string& InSlot );

    // Snapshots the world and starts writing the slot. False when the slot name is invalid or
    // there's no scene. A save started while another is still writing waits for it first.
    bool Save( const std::string& InSlot, const std::string& InName );
    // Queues a load for the end of the frame. False when the slot can't be read.
    bool Load( const std::string& InSlot );
    bool Delete( const std::string& InSlot );
    bool Exists( const std::string& InSlot ) const;
    bool ReadInfo( const std::string& InSlot, SlotInfo& OutInfo ) const;
    // Every readable slot, newest first.
    std::vector<SlotInfo> List() const;

    bool IsWriting() const;
    // Blocks until the write in flight (if any) has landed; returns whether it succeeded.
    bool WaitForWrites();
    // Seconds of play this run, starting from the loaded save's playtime.
    double GetPlaytime() const { return m_playtime; }

    // Once a frame, after rendering: counts playtime while the game runs and performs a queued load.
    void Update( float InUnscaledDeltaSeconds );

    // The file layer (also what the tests drive). WriteSlot writes Save.json then Meta.json
    // atomically into a slot folder (a logical path); ReadSlot reads Save.json back, migrating
    // older versions (and the world through SceneSerializer::MigrateToLatest).
    static bool WriteSlot( const std::string& InSlotDirectory, const json& InMeta, const json& InWorld );
    static bool ReadSlot( const std::string& InSlotDirectory, json& OutMeta, json& OutWorld, std::string* OutError = nullptr );
    // A stored save document upgraded to kVersion (OutNotes says what changed).
    static json Migrate( const json& InSave, std::string* OutNotes = nullptr );

private:
    std::future<bool> m_write;
    std::string m_writingSlot;
    std::string m_pendingLoad;
    double m_playtime = 0.0;
};
