#pragma once
#include "JSON.h"
#include "Math/Vector3.h"
#include "Net/NetReplay.h"
#include <array>
#include <atomic>
#include <cstdint>
#include <map>
#include <string>
#include <vector>

// Heatmaps from match replays (ME-215, NEXT-4): where players were, died and killed, over one
// replay or many of the same scene, on a grid over the ground (X, Z). Nothing here needs the
// engine running: ReplayDump's --heatmap builds one before the engine starts, the replay viewer
// and the "heatmap" console command (HeatmapOverlay) draw one over the world.
//
// Layers, each kept for the whole match ("all") and per group:
// - positions: every networked character's mover (format 3) every SampleTicks ticks, weighted by
//   the time a sample stands for, so a cell's value is seconds spent there; not while it's dead
//   (from its "kill" event to a "respawn" or "spawn" event about it, AliveEvents);
// - deaths:    each "kill" event (C#'s Replay.Event, the convention the timeline uses): at the
//              payload's "at" ([x, y, z], HeroShooter's death position), else where the event's
//              entity (the victim) is at that tick;
// - kills:     the same event, where the killer is at that tick: the payload's "killerNetId" (or
//              "killerId"), else whoever fired the newest shot that hit the victim in the
//              KillerShotSeconds before (a projectile's sweep counts for its instigator), but not
//              a hit whose shooter died after landing it (someone else finished the victim) nor
//              one by another player than the payload's "killer" names (the "player" of its
//              events). A killer that is dead by then (a rocket still in flight) is placed where
//              it last stood alive, never where its body was parked. A kill with no killer (a
//              fall) is a death only, counted as unattributed; one by the victim itself (a rocket
//              jump gone wrong) a death only.
//
// Groups: "hero:<name>" (the entity's prefab file name, "Striker" from .../Heroes/Striker.prefab,
// else its entity name) and "team:<name>" (the newest "team" in the payload of any event about the
// entity: a game that has teams says so in its pick or spawn events; a kill's "victimTeam" /
// "killerTeam" win for that kill).
//
// The JSON (ToJson / FromJson; "format": "mheatmap") keeps sparse cells per layer and group and
// what went in, so the viewer can draw one built from many replays. WritePng draws one layer (or an
// overview of all three) top-down: +X to the right, -Z at the top, one square per cell.
class NetReplayHeatmap
{
public:
    enum class Layer : uint8_t
    {
        Positions,
        Deaths,
        Kills,
        Count
    };
    static constexpr size_t kLayerCount = static_cast<size_t>( Layer::Count );
    static const char* LayerName( Layer InLayer );
    static bool ParseLayer( const std::string& InName, Layer& OutLayer );
    static constexpr int kFormatVersion = 1;

    struct Settings
    {
        float CellSize = 1.f;               // metres
        uint32_t SampleTicks = 6;           // a position sample every this many ticks (10 Hz at 60)
        std::string KillEvent = "kill";
        double KillerShotSeconds = 10.0;
        // A character killed isn't sampled for positions until one of these events about it (games park
        // the dead somewhere out of sight: HeroShooter's limbo spot).
        std::vector<std::string> AliveEvents = { "respawn", "spawn" };
        // A sample farther than this from the origin on X or Z (metres; 0: no limit) is dropped and
        // counted (GetOutOfRange): one character flung far off the map would stretch the grid, its PNGs and
        // the overlay over kilometres of nothing.
        float MaxDistance = 1000.f;
    };
    // The most cells a grid (GetGrid, the PNGs, the overlay) spans: one past it is empty (Width 0) and
    // WritePng says why.
    static constexpr int64_t kMaxGridCells = int64_t( 2048 ) * 2048;

    // A replay that went in.
    struct Source
    {
        std::string Path;
        std::string Map;
        std::string RecordedAt;
        uint32_t FirstTick = 0;
        uint32_t LastTick = 0;
        double Seconds = 0.0;
        uint32_t Kills = 0;
        uint32_t Samples = 0;
    };

    // A layer's cells for one group, dense over the heatmap's bounds (every layer's, so they line up):
    // Values[row * Width + column], row 0 at MinZ.
    struct Grid
    {
        int MinX = 0;
        int MinZ = 0;
        int Width = 0;
        int Height = 0;
        float CellSize = 1.f;
        std::vector<float> Values;
        float Max = 0.f;
        double Total = 0.0;
        float At( int InColumn, int InRow ) const { return Values[static_cast<size_t>( InRow ) * Width + InColumn]; }
    };

    NetReplayHeatmap() = default;
    explicit NetReplayHeatmap( const Settings& InSettings ) : m_settings( InSettings ) {}
    const Settings& GetSettings() const { return m_settings; }

    // One pass over InReader's ticks into the layers. False with OutError when a chunk won't decode,
    // the replay has no ticks, or it was recorded on another scene than the ones before it (its
    // scene hash; the heatmap would mix maps). InCancel (optional) stops it early (false, no error).
    // InPath names it in the sources.
    bool AddReplay( NetReplayReader& InReader, const std::string& InPath, std::string* OutError = nullptr, const std::atomic<bool>* InCancel = nullptr );
    // One sample into a layer: "all", "hero:<InHero>" and "team:<InTeam>" (either "" for none).
    void AddPoint( Layer InLayer, const Vector3& InPosition, const std::string& InHero, const std::string& InTeam, double InWeight = 1.0 );

    bool IsEmpty() const;
    const std::vector<Source>& GetSources() const { return m_sources; }
    const std::string& GetMap() const { return m_map; }
    uint64_t GetSceneHash() const { return m_sceneHash; }
    // Kills whose killer couldn't be told (deaths only), and suicides, over every replay.
    uint32_t GetUnattributedKills() const { return m_unattributed; }
    uint32_t GetSuicides() const { return m_suicides; }
    // Samples dropped past Settings::MaxDistance.
    uint32_t GetOutOfRange() const { return m_outOfRange; }

    // "all" first, then "hero:..." and "team:..." in order.
    std::vector<std::string> GetGroups() const;
    double GetTotal( Layer InLayer, const std::string& InGroup = "all" ) const;
    // The cell's value (0 when nothing went there).
    double ValueAt( Layer InLayer, const Vector3& InPosition, const std::string& InGroup = "all" ) const;
    // Over the cells of every layer and group; empty (Width 0) when there are none or it would span more
    // than kMaxGridCells.
    Grid GetGrid( Layer InLayer, const std::string& InGroup = "all" ) const;
    // The cell holding InPosition, and a cell's centre on the ground plane (y: the lowest sample
    // that went in there, any layer; NaN when none did).
    void CellOf( const Vector3& InPosition, int& OutX, int& OutZ ) const;
    Vector3 CellCentre( int InX, int InZ ) const;
    float FloorAt( int InX, int InZ ) const;

    json ToJson() const;
    // Replaces this one with InJson's (ToJson's format). False with OutError when it isn't one.
    bool FromJson( const json& InJson, std::string* OutError = nullptr );
    bool Save( const std::string& InPath, std::string* OutError = nullptr ) const;
    bool Load( const std::string& InPath, std::string* OutError = nullptr );

    // One layer as a heat ramp over a transparent background (empty cells clear), or the overview
    // (InOverview): time spent in blue-grey on dark, deaths red, kills green inside them.
    // InPixelsPerCell 0: chosen so the image is about 512 px across (2 to 16 a cell).
    bool WritePng( const std::string& InPath, Layer InLayer, const std::string& InGroup = "all", std::string* OutError = nullptr, int InPixelsPerCell = 0 ) const;
    bool WriteOverviewPng( const std::string& InPath, const std::string& InGroup = "all", std::string* OutError = nullptr, int InPixelsPerCell = 0 ) const;

    // The text summary: sources, totals per layer and group, the hottest cells.
    std::string Describe( size_t InHottest = 3 ) const;

    // The heat ramp both the PNGs and the overlays use: InT 0..1 -> RGB 0..1.
    static Vector3 Ramp( float InT );

private:
    using CellMap = std::map<int64_t, double>;
    static int64_t Key( int InX, int InZ );
    static void Unkey( int64_t InKey, int& OutX, int& OutZ );
    void Bounds( int& OutMinX, int& OutMinZ, int& OutMaxX, int& OutMaxZ ) const;
    // The bounds' width and height (64-bit: cells read from a file can be anywhere).
    void Extent( int64_t& OutWidth, int64_t& OutHeight ) const;
    bool WriteImage( const std::string& InPath, int InWidth, int InHeight, const std::vector<uint8_t>& InRgba, std::string* OutError ) const;
    int PixelsPerCell( int InWidth, int InHeight, int InRequested ) const;
    // Why GetGrid is empty: nothing in it, or too many cells.
    std::string GridError() const;

    Settings m_settings;
    std::array<std::map<std::string, CellMap>, kLayerCount> m_layers;
    std::map<int64_t, float> m_floor;
    std::vector<Source> m_sources;
    std::string m_map;
    uint64_t m_sceneHash = 0;
    uint32_t m_unattributed = 0;
    uint32_t m_suicides = 0;
    uint32_t m_outOfRange = 0;
};
