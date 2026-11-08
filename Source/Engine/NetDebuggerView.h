#pragma once
#include "JSON.h"
#include "Net/NetConnection.h"
#include "Net/NetDebug.h"
#include "Net/NetPrediction.h"
#include <cstdint>
#include <deque>
#include <string>
#include <vector>

class NetCore;

// What a client's Net Debugger shows (NET-7), apart from where it is drawn: the Player's window
// (NetLaunch::DrawNetDebugger) captures it from its own session every frame, and Havana's Net
// Debugger panel gets it from each PIE child as JSON (the console's "net view [tick]", over the
// child's --control-stdin) and draws it with the same code (DrawNetDebuggerView).
//
// The link (RTT, loss, the rate in / out, the clock's slack and dilation), the blob channel and the
// blobs on their way (a killcam arriving), the shots against the server's results, the
// top-bandwidth entities, the prediction counts, the misprediction timeline (one outcome a tick)
// and a mark for each mismatch with a record; one record in full (NetMispredictRecord: the field
// diff, the commands): the one asked for, else the newest.

// The ledger's totals in a line ("shots: 12 predicted, 12 results; 12 of 12 paired agree ...").
std::string DescribeShotTotals( const NetShotLedger& InLedger );

// A connection's rate in and out, from its byte counts, over the last InEvery seconds.
struct NetRateMeter
{
    double At = -1.0;
    uint64_t BytesIn = 0;
    uint64_t BytesOut = 0;
    double KbpsIn = 0.0;
    double KbpsOut = 0.0;

    void Sample( const NetStats& InStats, double InNow, double InEvery = 1.0 );
};

struct NetDebuggerView
{
    // A client session to show (false: nothing below but Reason).
    bool Client = false;
    std::string Reason;
    bool Connected = false;
    uint32_t ClientId = 0;
    uint32_t Tick = 0;

    // The link.
    float RoundTripMs = 0.f;
    float RoundTripDeviationMs = 0.f;
    float PacketLoss = 0.f;             // fraction
    double KbpsIn = 0.0;
    double KbpsOut = 0.0;
    float Slack = 0.f;                  // the server's word on this client's command slack, in ticks
    float TargetSlack = 0.f;
    float Dilation = 0.f;               // the clock's, a fraction (+0.01: running 1 % fast)
    int Resyncs = 0;
    uint64_t Hitches = 0;

    // The blob channel (ME-323) and each blob on its way.
    uint64_t BlobsReceived = 0;
    uint64_t BlobsSent = 0;
    uint64_t BlobsAcked = 0;
    uint64_t BlobsCancelled = 0;
    uint64_t BlobsCancelledByRemote = 0;
    uint64_t BlobSlicesSent = 0;
    uint64_t BlobSlicesResent = 0;
    uint64_t BlobSlicesDuplicate = 0;
    std::vector<NetBlobProgress> Blobs;

    // The shots against the server's results (NetShotLedger), the newest last.
    bool ShotsOn = false;               // the session compares them (NetCore::GetDebug().Shots)
    std::string ShotsSummary;
    struct Shot
    {
        uint32_t Tick = 0;
        double ViewTick = -1.0;
        bool HasServer = false;
        double RewoundTicks = 0.0;
        bool Clamped = false;
        int Verdict = 0;                // 0: one side only, 1: agree, 2: disagree
        std::string Describe;
    };
    std::vector<Shot> Shots;

    // The server's newest top-bandwidth report for this client (Tick 0: none yet).
    bool BandwidthOn = false;
    NetBandwidthReport Bandwidth;

    // The prediction (false: no pawn predicted).
    bool Predicting = false;
    int Corrections = 0;
    int Matches = 0;
    int InitialSync = 0;
    int LastResimTicks = 0;
    double LastResimMilliseconds = 0.0;
    float VisualOffsetMm = 0.f;
    // The timeline: an outcome for each tick First..Newest (HasTimeline: a tick was reconciled).
    bool HasTimeline = false;
    uint32_t First = 0;
    uint32_t Newest = 0;
    std::vector<NetPredictOutcome> Outcomes;
    // Each mismatch with a record, oldest first.
    struct Mark
    {
        uint32_t Tick = 0;
        bool InitialSync = false;
        std::string Describe;
    };
    std::vector<Mark> Marks;
    // One record in full: the one asked for (Capture's InDetailTick), else the newest.
    bool HasDetail = false;
    NetMispredictRecord Detail;

    // InNet's client session now, with InRate's rate. InDetailTick: the mismatch to give in full (0:
    // the newest).
    static NetDebuggerView Capture( NetCore& InNet, const NetRateMeter& InRate, uint32_t InDetailTick = 0 );
    // Every field, floats exactly (the detail in NetMispredict's own encoding).
    json ToJson() const;
    // False with OutError on a malformed document (nothing thrown).
    static bool FromJson( const json& InJson, NetDebuggerView& OutView, std::string* OutError = nullptr );
    NetPredictOutcome GetOutcome( uint32_t InTick ) const;
    // The number of corrections / initial-sync mismatches on the timeline.
    size_t CountOnTimeline( NetPredictOutcome InOutcome ) const;
};

// A view's link over time, for the graphs: a sample each time Add is given a new view, at most
// every InEvery seconds, kSamples kept.
struct NetDebuggerHistory
{
    static constexpr size_t kSamples = 240;
    struct Sample
    {
        float RoundTripMs = 0.f;
        float LossPercent = 0.f;
        float KbpsIn = 0.f;
        float KbpsOut = 0.f;
        float Slack = 0.f;
        float DilationPercent = 0.f;
    };
    std::deque<Sample> Samples;
    double LastAt = -1.0;
    double Every = 0.25;

    void Add( const NetDebuggerView& InView, double InNow );
    // A sample from elsewhere (Havana's panel: the server's end of a connection).
    void Add( const Sample& InSample, double InNow );
    void Clear();
};

// What the window keeps between frames.
struct NetDebuggerUI
{
    bool HasSelected = false;
    uint32_t Selected = 0;
    std::string LastExport;
    bool DrawOverlays = true;
};

// What was asked for this frame.
struct NetDebuggerActions
{
    bool Export = false;
    uint32_t ExportTick = 0;
    bool OverlaysChanged = false;   // UI.DrawOverlays was toggled
    bool SelectionChanged = false;  // a mark was clicked (UI.Selected)
};

// Draws InView into the current ImGui window (ImGui builds; elsewhere nothing). InHistory, when
// given, draws the link's graphs (RTT, loss, the rate, slack, dilation) under its numbers.
NetDebuggerActions DrawNetDebuggerView( const NetDebuggerView& InView, NetDebuggerUI& InUI, const NetDebuggerHistory* InHistory = nullptr );
// The graphs alone (InDilation false: the clock's dilation isn't known here, a server's end).
void DrawNetDebuggerGraphs( const NetDebuggerHistory& InHistory, bool InDilation = true );
