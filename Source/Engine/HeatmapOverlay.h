#pragma once
#include "JSON.h"
#include "Math/Vector3.h"
#include "Math/Vector4.h"
#include "Net/NetReplayHeatmap.h"
#include <atomic>
#include <future>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

class World;

// A replay heatmap (ME-215, NetReplayHeatmap.h) drawn over the world as a ground overlay: one
// translucent square per cell, coloured by the heat ramp (NetReplayHeatmap::Ramp of the square root
// of its share of the hottest cell), with DebugDraw, so every camera view shows it: the Player's
// game cameras, the replay viewer's, and Havana's scene view.
//
// One per process. The engine draws it every frame (Engine::Run, editor and Player alike) and
// registers its console command:
//
//   heatmap                          what's loaded and shown
//   heatmap load <file.json> [layer] [group]    a heatmap ReplayDump --heatmap wrote
//   heatmap build <replay|folder> ...           from replays, on the background lane
//   heatmap layer positions|deaths|kills
//   heatmap group <all|hero:Name|team:Name>     (heatmap groups lists them)
//   heatmap on|off|clear
//   heatmap cancel                              a build still running
//   heatmap opacity <0..1>
//
// Paths may start project:// (the project's folder). Any Player or Havana takes one at start-up:
// --heatmap-overlay <file.json> [--heatmap-overlay-layer LAYER[:GROUP]]; the edscript's
// "console heatmap load ..." reaches it in Havana; the replay viewer's --replay-heatmap and
// "replay heatmap" set it from the replay playing.
//
// Each cell is drawn at the ground under it: a ray down the world's physics from just above the
// lowest sample that went into the cell (characters' capsules ignored), found once per world, scene
// and heatmap (a scene load finds them again: OnSceneLoaded); where nothing is hit, that sample
// less 0.9 m (a capsule's centre to its feet). A heatmap recorded on another scene than the one
// loaded is still drawn, with a warning (logged once, and in Describe / Report).
//
// A build is cancelled by Clear, "heatmap cancel" and Engine shutdown (Cancel, before the background
// lane is joined), between ticks of the replay it is reading.
class HeatmapOverlay
{
public:
    static HeatmapOverlay& Get();
    // The console's "heatmap" command (once).
    static void RegisterCommands();

    bool Load( const std::string& InPath, std::string& OutError );
    // Builds from replays on the background lane; shown once built (the console's "heatmap" says how
    // it went). False when one is building already.
    bool Build( const std::vector<std::string>& InPaths, const NetReplayHeatmap::Settings& InSettings = NetReplayHeatmap::Settings() );
    bool IsBuilding() const { return m_build.valid(); }
    void Set( NetReplayHeatmap InHeatmap, const std::string& InSource );
    void Clear();
    // Stops a build still running (its result is dropped). False when none was.
    bool Cancel();
    // The command line's --heatmap-overlay (Engine start-up; logs what it did).
    void ApplyCommandLine();
    // A scene loaded into the world (Engine::LoadSceneInternal): the cells find the ground again, and a
    // heatmap of another map is warned about.
    void OnSceneLoaded( const std::string& InScene );
    // The heatmap's map is the scene loaded (or either is unknown).
    bool MatchesScene() const;
    bool HasHeatmap() const { return m_heatmap != nullptr; }
    const NetReplayHeatmap* GetHeatmap() const { return m_heatmap.get(); }

    void SetShown( bool InShown ) { m_shown = InShown; }
    bool IsShown() const { return m_shown; }
    void SetLayer( NetReplayHeatmap::Layer InLayer );
    NetReplayHeatmap::Layer GetLayer() const { return m_layer; }
    // False when the heatmap has no such group.
    bool SetGroup( const std::string& InGroup );
    const std::string& GetGroup() const { return m_group; }
    void SetOpacity( float InOpacity );

    // Every frame: polls a build, then draws the cells over InWorld while shown.
    void Draw( World& InWorld );
    // The cells drawn last (after Draw; a test or report reads them).
    struct Cell
    {
        Vector3 Centre;     // on the ground
        float Value = 0.f;
        Vector4 Colour;
    };
    const std::vector<Cell>& GetCells() const { return m_cells; }

    std::string Describe() const;
    json Report() const;
    // The console command's work (tests run it without the console).
    std::string RunCommand( const std::vector<std::string>& InArgs );

private:
    void Invalidate() { m_builtFor = nullptr; m_cells.clear(); }
    void BuildCells( World& InWorld );
    void WarnIfOtherScene();

    std::unique_ptr<NetReplayHeatmap> m_heatmap;
    std::string m_source;
    NetReplayHeatmap::Layer m_layer = NetReplayHeatmap::Layer::Positions;
    std::string m_group = "all";
    float m_opacity = 0.7f;
    bool m_shown = true;
    std::vector<Cell> m_cells;
    const World* m_builtFor = nullptr;
    // A build from replays on the lane: the heatmap (or why it failed) and what went in.
    struct Built
    {
        std::unique_ptr<NetReplayHeatmap> Heatmap;
        std::string Error;
        std::string Source;
    };
    std::future<Built> m_build;
    std::shared_ptr<std::atomic<bool>> m_cancel;
    std::string m_buildError;
    std::string m_scene;
};
