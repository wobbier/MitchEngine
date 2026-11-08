#include "PCH.h"
#include "Engine/NetDebuggerView.h"
#include "Cores/NetCore.h"
#include "Net/NetSession.h"
#if USING( ME_IMGUI )
#include <imgui.h>
#endif
#include <algorithm>
#include <cmath>
#include <cstdio>

void NetRateMeter::Sample( const NetStats& InStats, double InNow, double InEvery )
{
    if( At < 0.0 || InStats.BytesReceived < BytesIn || InStats.BytesSent < BytesOut )
    {
        At = InNow;
        BytesIn = InStats.BytesReceived;
        BytesOut = InStats.BytesSent;
        return;
    }
    if( InNow - At < InEvery )
    {
        return;
    }
    const double seconds = InNow - At;
    KbpsIn = static_cast<double>( InStats.BytesReceived - BytesIn ) * 8.0 / 1000.0 / seconds;
    KbpsOut = static_cast<double>( InStats.BytesSent - BytesOut ) * 8.0 / 1000.0 / seconds;
    At = InNow;
    BytesIn = InStats.BytesReceived;
    BytesOut = InStats.BytesSent;
}


std::string DescribeShotTotals( const NetShotLedger& InLedger )
{
    const NetShotLedger::Totals& totals = InLedger.GetTotals();
    char line[384];
    std::snprintf( line, sizeof( line ), "shots: %llu predicted, %llu results; %llu of %llu paired agree (%.0f%%, %llu the same part), %llu disagree; %llu client only, %llu server only, %zu waiting; "
        "hit points %.1f mm apart on average (%.1f at most)", static_cast<unsigned long long>( totals.Predicted ), static_cast<unsigned long long>( totals.Results ),
        static_cast<unsigned long long>( totals.Agreed ), static_cast<unsigned long long>( totals.Paired ),
        totals.Paired > 0 ? 100.0 * static_cast<double>( totals.Agreed ) / static_cast<double>( totals.Paired ) : 0.0, static_cast<unsigned long long>( totals.SamePart ),
        static_cast<unsigned long long>( totals.Disagreed ), static_cast<unsigned long long>( totals.ClientOnly ), static_cast<unsigned long long>( totals.ServerOnly ),
        InLedger.GetPendingCount(), totals.PointPairs > 0 ? totals.SumPointDelta / static_cast<double>( totals.PointPairs ) * 1000.0 : 0.0, totals.MaxPointDelta * 1000.0 );
    return line;
}


NetDebuggerView NetDebuggerView::Capture( NetCore& InNet, const NetRateMeter& InRate, uint32_t InDetailTick )
{
    NetDebuggerView view;
    NetClient* client = InNet.GetClient();
    if( !client )
    {
        view.Reason = InNet.IsServer() ? "a server: the Net Debugger shows a client's session" : "no client session";
        return view;
    }
    view.Client = true;
    view.Connected = client->GetState() == NetClient::State::Connected;
    view.ClientId = client->GetClientId();
    view.Tick = client->GetClock().GetTick();

    const NetStats& stats = client->GetConnection().GetStats();
    view.RoundTripMs = stats.RoundTripMs;
    view.RoundTripDeviationMs = stats.RoundTripDeviationMs;
    view.PacketLoss = stats.PacketLoss;
    view.KbpsIn = InRate.KbpsIn;
    view.KbpsOut = InRate.KbpsOut;
    view.Slack = client->GetServerSlack();
    view.TargetSlack = client->GetClock().GetTargetSlack();
    view.Dilation = client->GetClock().GetDilation();
    view.Resyncs = client->GetClock().GetResyncCount();
    view.Hitches = client->GetHitchCount();

    view.BlobsReceived = stats.BlobsReceived;
    view.BlobsSent = stats.BlobsSent;
    view.BlobsAcked = stats.BlobsAcked;
    view.BlobsCancelled = stats.BlobsCancelled;
    view.BlobsCancelledByRemote = stats.BlobsCancelledByRemote;
    view.BlobSlicesSent = stats.BlobSlicesSent;
    view.BlobSlicesResent = stats.BlobSlicesResent;
    view.BlobSlicesDuplicate = stats.BlobSlicesDuplicate;
    view.Blobs = client->GetConnection().GetBlobProgress();

    view.ShotsOn = InNet.GetDebug().Shots;
    view.ShotsSummary = DescribeShotTotals( InNet.GetShotLedger() );
    for( const NetShotComparison& comparison : InNet.GetShotLedger().GetRecent() )
    {
        Shot shot;
        shot.Tick = comparison.HasClient ? comparison.Client.Tick : comparison.Server.CommandTick;
        shot.ViewTick = comparison.HasServer ? comparison.Server.Shot.ViewTick : comparison.Client.ViewTick;
        shot.HasServer = comparison.HasServer;
        shot.RewoundTicks = comparison.HasServer ? comparison.Server.Shot.RewoundTicks : 0.0;
        shot.Clamped = comparison.HasServer && comparison.Server.Shot.Clamped;
        shot.Verdict = !comparison.HasClient || !comparison.HasServer ? 0 : comparison.Agrees() ? 1 : 2;
        shot.Describe = comparison.Describe();
        view.Shots.push_back( std::move( shot ) );
    }
    view.BandwidthOn = InNet.GetDebug().Bandwidth;
    view.Bandwidth = InNet.GetBandwidthReport();

    const NetPredictor* predictor = InNet.GetPredictor();
    if( !predictor )
    {
        return view;
    }
    view.Predicting = true;
    view.Corrections = predictor->GetCorrectionCount();
    view.Matches = predictor->GetMatchCount();
    view.InitialSync = predictor->GetInitialSyncCount();
    view.LastResimTicks = predictor->GetLastResimTicks();
    view.LastResimMilliseconds = predictor->GetLastResimMilliseconds();
    view.VisualOffsetMm = predictor->GetVisualOffset().Length() * 1000.f;
    uint32_t newest = 0;
    if( predictor->GetNewestReconciled( newest ) )
    {
        const uint32_t span = predictor->GetTimelineTicks();
        view.HasTimeline = true;
        view.Newest = newest;
        view.First = newest + 1 >= span ? newest + 1 - span : 0;
        view.Outcomes.reserve( newest - view.First + 1 );
        for( uint32_t tick = view.First; tick <= newest; ++tick )
        {
            view.Outcomes.push_back( predictor->GetOutcome( tick ) );
        }
    }
    for( const NetMispredictRecord& record : predictor->GetMispredicts() )
    {
        view.Marks.push_back( { record.Tick, record.InitialSync, record.Describe() } );
    }
    const NetMispredictRecord* detail = InDetailTick != 0 ? predictor->FindMispredict( InDetailTick ) : nullptr;
    if( !detail && !predictor->GetMispredicts().empty() )
    {
        detail = &predictor->GetMispredicts().back();
    }
    if( detail )
    {
        view.HasDetail = true;
        view.Detail = *detail;
    }
    return view;
}


json NetDebuggerView::ToJson() const
{
    json out;
    out["client"] = Client;
    if( !Client )
    {
        out["reason"] = Reason;
        return out;
    }
    out["connected"] = Connected;
    out["clientId"] = ClientId;
    out["tick"] = Tick;
    out["link"] = { { "rtt", RoundTripMs }, { "rttDeviation", RoundTripDeviationMs }, { "loss", PacketLoss }, { "kbpsIn", KbpsIn }, { "kbpsOut", KbpsOut }, { "slack", Slack },
        { "targetSlack", TargetSlack }, { "dilation", Dilation }, { "resyncs", Resyncs }, { "hitches", Hitches } };
    json blobs = json::array();
    for( const NetBlobProgress& blob : Blobs )
    {
        blobs.push_back( { { "id", blob.Id }, { "out", blob.Outgoing }, { "started", blob.Started }, { "size", blob.Size }, { "done", blob.DoneBytes },
            { "contiguous", blob.ContiguousBytes }, { "startedAt", blob.StartedAt } } );
    }
    out["blobs"] = { { "received", BlobsReceived }, { "sent", BlobsSent }, { "acked", BlobsAcked }, { "cancelled", BlobsCancelled }, { "cancelledByRemote", BlobsCancelledByRemote },
        { "slicesSent", BlobSlicesSent }, { "slicesResent", BlobSlicesResent }, { "slicesDuplicate", BlobSlicesDuplicate }, { "progress", blobs } };
    json shots = json::array();
    for( const Shot& shot : Shots )
    {
        shots.push_back( { { "tick", shot.Tick }, { "viewTick", shot.ViewTick }, { "server", shot.HasServer }, { "rewound", shot.RewoundTicks }, { "clamped", shot.Clamped },
            { "verdict", shot.Verdict }, { "describe", shot.Describe } } );
    }
    out["shots"] = { { "on", ShotsOn }, { "summary", ShotsSummary }, { "recent", shots } };
    json top = json::array();
    for( const NetEntityBytes& entity : Bandwidth.Top )
    {
        top.push_back( { { "netId", entity.NetId }, { "bits", entity.Bits }, { "bytesPerSecond", entity.BytesPerSecond }, { "name", entity.Name } } );
    }
    out["bandwidth"] = { { "on", BandwidthOn }, { "tick", Bandwidth.Tick }, { "seconds", Bandwidth.Seconds }, { "snapshotBytes", Bandwidth.SnapshotBytes },
        { "overheadBits", Bandwidth.OverheadBits }, { "top", top } };
    out["predicting"] = Predicting;
    if( !Predicting )
    {
        return out;
    }
    json prediction = { { "corrections", Corrections }, { "matches", Matches }, { "initialSync", InitialSync }, { "lastResimTicks", LastResimTicks },
        { "lastResimMs", LastResimMilliseconds }, { "visualOffsetMm", VisualOffsetMm } };
    if( HasTimeline )
    {
        // One digit a tick (NetPredictOutcome), First..Newest.
        std::string outcomes;
        outcomes.reserve( Outcomes.size() );
        for( NetPredictOutcome outcome : Outcomes )
        {
            outcomes.push_back( static_cast<char>( '0' + static_cast<int>( outcome ) ) );
        }
        prediction["timeline"] = { { "first", First }, { "newest", Newest }, { "outcomes", outcomes } };
    }
    json marks = json::array();
    for( const Mark& mark : Marks )
    {
        marks.push_back( { { "tick", mark.Tick }, { "initialSync", mark.InitialSync }, { "describe", mark.Describe } } );
    }
    prediction["marks"] = marks;
    if( HasDetail )
    {
        prediction["detail"] = json::parse( NetMispredict::ToJson( Detail ), nullptr, false );
    }
    out["prediction"] = prediction;
    return out;
}


bool NetDebuggerView::FromJson( const json& InJson, NetDebuggerView& OutView, std::string* OutError )
{
    OutView = NetDebuggerView();
    try
    {
        if( !InJson.is_object() )
        {
            throw std::runtime_error( "not an object" );
        }
        OutView.Client = InJson.at( "client" ).get<bool>();
        if( !OutView.Client )
        {
            OutView.Reason = InJson.value( "reason", std::string() );
            return true;
        }
        OutView.Connected = InJson.at( "connected" ).get<bool>();
        OutView.ClientId = InJson.at( "clientId" ).get<uint32_t>();
        OutView.Tick = InJson.at( "tick" ).get<uint32_t>();
        const json& link = InJson.at( "link" );
        OutView.RoundTripMs = link.at( "rtt" ).get<float>();
        OutView.RoundTripDeviationMs = link.at( "rttDeviation" ).get<float>();
        OutView.PacketLoss = link.at( "loss" ).get<float>();
        OutView.KbpsIn = link.at( "kbpsIn" ).get<double>();
        OutView.KbpsOut = link.at( "kbpsOut" ).get<double>();
        OutView.Slack = link.at( "slack" ).get<float>();
        OutView.TargetSlack = link.at( "targetSlack" ).get<float>();
        OutView.Dilation = link.at( "dilation" ).get<float>();
        OutView.Resyncs = link.at( "resyncs" ).get<int>();
        OutView.Hitches = link.at( "hitches" ).get<uint64_t>();
        const json& blobs = InJson.at( "blobs" );
        OutView.BlobsReceived = blobs.at( "received" ).get<uint64_t>();
        OutView.BlobsSent = blobs.at( "sent" ).get<uint64_t>();
        OutView.BlobsAcked = blobs.at( "acked" ).get<uint64_t>();
        OutView.BlobsCancelled = blobs.at( "cancelled" ).get<uint64_t>();
        OutView.BlobsCancelledByRemote = blobs.at( "cancelledByRemote" ).get<uint64_t>();
        OutView.BlobSlicesSent = blobs.at( "slicesSent" ).get<uint64_t>();
        OutView.BlobSlicesResent = blobs.at( "slicesResent" ).get<uint64_t>();
        OutView.BlobSlicesDuplicate = blobs.at( "slicesDuplicate" ).get<uint64_t>();
        for( const json& blob : blobs.at( "progress" ) )
        {
            NetBlobProgress progress;
            progress.Id = blob.at( "id" ).get<uint32_t>();
            progress.Outgoing = blob.at( "out" ).get<bool>();
            progress.Started = blob.at( "started" ).get<bool>();
            progress.Size = blob.at( "size" ).get<size_t>();
            progress.DoneBytes = blob.at( "done" ).get<size_t>();
            progress.ContiguousBytes = blob.at( "contiguous" ).get<size_t>();
            progress.StartedAt = blob.at( "startedAt" ).get<double>();
            OutView.Blobs.push_back( progress );
        }
        const json& shots = InJson.at( "shots" );
        OutView.ShotsOn = shots.at( "on" ).get<bool>();
        OutView.ShotsSummary = shots.at( "summary" ).get<std::string>();
        for( const json& item : shots.at( "recent" ) )
        {
            Shot shot;
            shot.Tick = item.at( "tick" ).get<uint32_t>();
            shot.ViewTick = item.at( "viewTick" ).get<double>();
            shot.HasServer = item.at( "server" ).get<bool>();
            shot.RewoundTicks = item.at( "rewound" ).get<double>();
            shot.Clamped = item.at( "clamped" ).get<bool>();
            shot.Verdict = item.at( "verdict" ).get<int>();
            shot.Describe = item.at( "describe" ).get<std::string>();
            OutView.Shots.push_back( std::move( shot ) );
        }
        const json& bandwidth = InJson.at( "bandwidth" );
        OutView.BandwidthOn = bandwidth.at( "on" ).get<bool>();
        OutView.Bandwidth.Tick = bandwidth.at( "tick" ).get<uint32_t>();
        OutView.Bandwidth.Seconds = bandwidth.at( "seconds" ).get<double>();
        OutView.Bandwidth.SnapshotBytes = bandwidth.at( "snapshotBytes" ).get<uint64_t>();
        OutView.Bandwidth.OverheadBits = bandwidth.at( "overheadBits" ).get<uint64_t>();
        for( const json& item : bandwidth.at( "top" ) )
        {
            NetEntityBytes entity;
            entity.NetId = item.at( "netId" ).get<uint32_t>();
            entity.Bits = item.at( "bits" ).get<uint64_t>();
            entity.BytesPerSecond = item.at( "bytesPerSecond" ).get<double>();
            entity.Name = item.at( "name" ).get<std::string>();
            OutView.Bandwidth.Top.push_back( std::move( entity ) );
        }
        OutView.Predicting = InJson.at( "predicting" ).get<bool>();
        if( !OutView.Predicting )
        {
            return true;
        }
        const json& prediction = InJson.at( "prediction" );
        OutView.Corrections = prediction.at( "corrections" ).get<int>();
        OutView.Matches = prediction.at( "matches" ).get<int>();
        OutView.InitialSync = prediction.at( "initialSync" ).get<int>();
        OutView.LastResimTicks = prediction.at( "lastResimTicks" ).get<int>();
        OutView.LastResimMilliseconds = prediction.at( "lastResimMs" ).get<double>();
        OutView.VisualOffsetMm = prediction.at( "visualOffsetMm" ).get<float>();
        if( prediction.contains( "timeline" ) )
        {
            const json& timeline = prediction.at( "timeline" );
            OutView.HasTimeline = true;
            OutView.First = timeline.at( "first" ).get<uint32_t>();
            OutView.Newest = timeline.at( "newest" ).get<uint32_t>();
            const std::string outcomes = timeline.at( "outcomes" ).get<std::string>();
            if( OutView.Newest < OutView.First || outcomes.size() != static_cast<size_t>( OutView.Newest - OutView.First ) + 1 )
            {
                throw std::runtime_error( "the timeline's outcomes don't cover first..newest" );
            }
            for( char outcome : outcomes )
            {
                if( outcome < '0' || outcome > '3' )
                {
                    throw std::runtime_error( std::string( "unknown outcome '" ) + outcome + "'" );
                }
                OutView.Outcomes.push_back( static_cast<NetPredictOutcome>( outcome - '0' ) );
            }
        }
        for( const json& item : prediction.at( "marks" ) )
        {
            OutView.Marks.push_back( { item.at( "tick" ).get<uint32_t>(), item.at( "initialSync" ).get<bool>(), item.at( "describe" ).get<std::string>() } );
        }
        if( prediction.contains( "detail" ) )
        {
            std::string error;
            if( !NetMispredict::FromJson( prediction.at( "detail" ).dump(), OutView.Detail, &error ) )
            {
                throw std::runtime_error( "the detail: " + error );
            }
            OutView.HasDetail = true;
        }
    }
    catch( const std::exception& InError )
    {
        if( OutError )
        {
            *OutError = InError.what();
        }
        OutView = NetDebuggerView();
        return false;
    }
    return true;
}


NetPredictOutcome NetDebuggerView::GetOutcome( uint32_t InTick ) const
{
    if( !HasTimeline || InTick < First || InTick > Newest )
    {
        return NetPredictOutcome::None;
    }
    return Outcomes[InTick - First];
}


size_t NetDebuggerView::CountOnTimeline( NetPredictOutcome InOutcome ) const
{
    return static_cast<size_t>( std::count( Outcomes.begin(), Outcomes.end(), InOutcome ) );
}


void NetDebuggerHistory::Add( const NetDebuggerView& InView, double InNow )
{
    if( !InView.Client )
    {
        return;
    }
    Sample sample;
    sample.RoundTripMs = InView.RoundTripMs;
    sample.LossPercent = InView.PacketLoss * 100.f;
    sample.KbpsIn = static_cast<float>( InView.KbpsIn );
    sample.KbpsOut = static_cast<float>( InView.KbpsOut );
    sample.Slack = InView.Slack;
    sample.DilationPercent = InView.Dilation * 100.f;
    Add( sample, InNow );
}


void NetDebuggerHistory::Add( const Sample& InSample, double InNow )
{
    if( LastAt >= 0.0 && InNow - LastAt < Every )
    {
        return;
    }
    LastAt = InNow;
    Samples.push_back( InSample );
    while( Samples.size() > kSamples )
    {
        Samples.pop_front();
    }
}


void NetDebuggerHistory::Clear()
{
    Samples.clear();
    LastAt = -1.0;
}


#if USING( ME_IMGUI )
namespace
{
    // One graph: InTitle and the newest value over it, the range from 0 (or below, for dilation).
    void Graph( const char* InId, const char* InFormat, const std::vector<float>& InValues, bool InSigned = false )
    {
        if( InValues.empty() )
        {
            return;
        }
        float low = 0.f;
        float high = 1.f;
        for( float value : InValues )
        {
            high = std::max( high, value );
            low = std::min( low, value );
        }
        if( InSigned )
        {
            const float extent = std::max( std::abs( low ), std::abs( high ) );
            low = -extent;
            high = extent;
        }
        char overlay[96];
        std::snprintf( overlay, sizeof( overlay ), InFormat, InValues.back(), high );
        ImGui::PlotLines( InId, InValues.data(), static_cast<int>( InValues.size() ), 0, overlay, low, high * 1.1f, ImVec2( -1.f, ImGui::GetTextLineHeight() * 2.5f ) );
    }
}
#endif


void DrawNetDebuggerGraphs( const NetDebuggerHistory& InHistory, bool InDilation )
{
#if USING( ME_IMGUI )
    if( InHistory.Samples.empty() )
    {
        return;
    }
    std::vector<float> values( InHistory.Samples.size() );
    auto graph = [&]( const char* InId, const char* InFormat, float NetDebuggerHistory::Sample::*InField, bool InSigned = false ) {
        for( size_t i = 0; i < InHistory.Samples.size(); ++i )
        {
            values[i] = InHistory.Samples[i].*InField;
        }
        ImGui::TableNextColumn();
        Graph( InId, InFormat, values, InSigned );
    };
    const float seconds = static_cast<float>( InHistory.Samples.size() * InHistory.Every );
    ImGui::TextDisabled( "the last %.0f s", seconds );
    // Two to a row: RTT and loss, the rate in and out, slack and dilation.
    if( !ImGui::BeginTable( "##NetGraphs", 2, ImGuiTableFlags_SizingStretchSame ) )
    {
        return;
    }
    graph( "##GraphRtt", "RTT %.1f ms (peak %.0f)", &NetDebuggerHistory::Sample::RoundTripMs );
    graph( "##GraphLoss", "loss %.1f %% (peak %.1f)", &NetDebuggerHistory::Sample::LossPercent );
    graph( "##GraphIn", "in %.1f kbps (peak %.0f)", &NetDebuggerHistory::Sample::KbpsIn );
    graph( "##GraphOut", "out %.1f kbps (peak %.0f)", &NetDebuggerHistory::Sample::KbpsOut );
    graph( "##GraphSlack", "slack %.2f ticks (peak %.2f)", &NetDebuggerHistory::Sample::Slack, true );
    if( InDilation )
    {
        graph( "##GraphDilation", "dilation %+.2f %% (range %.2f)", &NetDebuggerHistory::Sample::DilationPercent, true );
    }
    ImGui::EndTable();
#else
    (void)InHistory;
    (void)InDilation;
#endif
}


#if USING( ME_IMGUI )
namespace
{
    // The prediction: its counts, the timeline (click a mark to pick it), the mark's record and Export.
    void DrawPrediction( const NetDebuggerView& InView, NetDebuggerUI& InUI, NetDebuggerActions& actions )
    {
        if( !InView.Predicting )
        {
            ImGui::TextUnformatted( "Nothing predicted (no pawn owned)." );
            return;
        }
        ImGui::Text( "Prediction: %d corrections, %d matches, %d initial sync; last resim %d ticks in %.3f ms; visual offset %.1f mm", InView.Corrections, InView.Matches,
            InView.InitialSync, InView.LastResimTicks, InView.LastResimMilliseconds, InView.VisualOffsetMm );

        auto findMark = [&InView]( uint32_t InTick ) -> const NetDebuggerView::Mark* {
            for( const NetDebuggerView::Mark& mark : InView.Marks )
            {
                if( mark.Tick == InTick )
                {
                    return &mark;
                }
            }
            return nullptr;
        };
        // The timeline: the last 10 s of reconciled ticks, one column each (green match, red correction,
        // grey initial sync, dark not reconciled). Hover for the tick; click a mark to inspect it.
        if( InView.HasTimeline )
        {
            const uint32_t first = InView.First;
            const uint32_t newest = InView.Newest;
            const uint32_t span = std::max<uint32_t>( 1, newest - first + 1 );
            const float width = std::max( 100.f, ImGui::GetContentRegionAvail().x );
            const float height = 28.f;
            const ImVec2 origin = ImGui::GetCursorScreenPos();
            ImGui::InvisibleButton( "##MispredictTimeline", ImVec2( width, height ) );
            const bool hovered = ImGui::IsItemHovered();
            const bool clicked = ImGui::IsItemClicked();
            ImDrawList* draw = ImGui::GetWindowDrawList();
            draw->AddRectFilled( origin, ImVec2( origin.x + width, origin.y + height ), IM_COL32( 25, 25, 30, 255 ) );
            const float perTick = width / static_cast<float>( span );
            auto xOf = [&]( uint32_t InTick ) { return origin.x + static_cast<float>( InTick - first ) * perTick; };
            // Every reconciled tick from the timeline itself: under a steady desync only the newest
            // kMaxMispredicts mismatches keep a record, and the rest must still read red.
            for( uint32_t tick = first; tick <= newest; ++tick )
            {
                const NetPredictOutcome outcome = InView.GetOutcome( tick );
                if( outcome == NetPredictOutcome::Match )
                {
                    draw->AddRectFilled( ImVec2( xOf( tick ), origin.y + height * 0.55f ), ImVec2( xOf( tick ) + std::max( perTick, 1.f ), origin.y + height ), IM_COL32( 60, 170, 80, 255 ) );
                }
                else if( outcome == NetPredictOutcome::Correction || outcome == NetPredictOutcome::InitialSync )
                {
                    const ImU32 color = outcome == NetPredictOutcome::InitialSync ? IM_COL32( 150, 150, 150, 255 ) : IM_COL32( 230, 60, 50, 255 );
                    draw->AddRectFilled( ImVec2( xOf( tick ), origin.y ), ImVec2( xOf( tick ) + std::max( perTick, 1.f ), origin.y + height ), color );
                }
            }
            // The mismatches with a record on top, wide enough to see and click.
            for( const NetDebuggerView::Mark& mark : InView.Marks )
            {
                if( mark.Tick < first || mark.Tick > newest )
                {
                    continue;
                }
                const bool selected = InUI.HasSelected && InUI.Selected == mark.Tick;
                const ImU32 color = mark.InitialSync ? IM_COL32( 150, 150, 150, 255 ) : selected ? IM_COL32( 255, 220, 60, 255 ) : IM_COL32( 230, 60, 50, 255 );
                draw->AddRectFilled( ImVec2( xOf( mark.Tick ) - 1.f, origin.y ), ImVec2( xOf( mark.Tick ) + std::max( perTick, 2.f ), origin.y + height ), color );
            }
            if( hovered )
            {
                const float mouse = ImGui::GetIO().MousePos.x;
                const uint32_t tick = first + static_cast<uint32_t>( std::clamp( ( mouse - origin.x ) / perTick, 0.f, static_cast<float>( span - 1 ) ) );
                // The nearest mismatch within 4 px.
                const NetDebuggerView::Mark* closest = nullptr;
                for( const NetDebuggerView::Mark& mark : InView.Marks )
                {
                    if( mark.Tick >= first && std::abs( xOf( mark.Tick ) - mouse ) <= 4.f && ( !closest || std::abs( xOf( mark.Tick ) - mouse ) < std::abs( xOf( closest->Tick ) - mouse ) ) )
                    {
                        closest = &mark;
                    }
                }
                static const char* names[] = { "not reconciled", "match", "correction", "initial sync" };
                ImGui::BeginTooltip();
                if( closest )
                {
                    ImGui::Text( "tick %u: %s", closest->Tick, closest->InitialSync ? "initial sync" : "correction" );
                    ImGui::TextUnformatted( closest->Describe.c_str() );
                }
                else
                {
                    const NetPredictOutcome outcome = InView.GetOutcome( tick );
                    ImGui::Text( "tick %u: %s", tick, names[static_cast<int>( outcome )] );
                    if( ( outcome == NetPredictOutcome::Correction || outcome == NetPredictOutcome::InitialSync ) && !findMark( tick ) )
                    {
                        ImGui::Text( "(no record: only the newest %zu mismatches keep one)", NetPredictor::kMaxMispredicts );
                    }
                }
                ImGui::EndTooltip();
                if( clicked && closest )
                {
                    InUI.HasSelected = true;
                    InUI.Selected = closest->Tick;
                    actions.SelectionChanged = true;
                }
            }
            ImGui::Text( "ticks %u..%u (%.0f s)", first, newest, NetPredictor::kTimelineSeconds );
        }
        else
        {
            ImGui::TextUnformatted( "No tick reconciled yet." );
        }

        // The mark picked (else the newest mismatch): what differed, the commands, export.
        ImGui::Separator();
        if( InView.Marks.empty() )
        {
            ImGui::TextUnformatted( "No mispredictions in the last 10 s." );
            return;
        }
        const bool selectedGone = InUI.HasSelected && !findMark( InUI.Selected );
        if( !InView.HasDetail || ( InUI.HasSelected && !selectedGone && InView.Detail.Tick != InUI.Selected ) )
        {
            // A remote view: the record asked for is on its way.
            const NetDebuggerView::Mark* mark = InUI.HasSelected && !selectedGone ? findMark( InUI.Selected ) : &InView.Marks.back();
            ImGui::Text( "Tick %u%s: %s", mark->Tick, mark->InitialSync ? " (initial sync)" : "", mark->Describe.c_str() );
            ImGui::TextDisabled( "(its record is on its way)" );
            return;
        }
        const NetMispredictRecord& record = InView.Detail;
        ImGui::Text( "Tick %u%s: %s", record.Tick, record.InitialSync ? " (initial sync)" : "", record.Describe().c_str() );
        if( ImGui::BeginTable( "##MispredictFields", 4, ImGuiTableFlags_Borders | ImGuiTableFlags_SizingStretchProp ) )
        {
            ImGui::TableSetupColumn( "" );
            ImGui::TableSetupColumn( "predicted" );
            ImGui::TableSetupColumn( "server" );
            ImGui::TableSetupColumn( "delta" );
            ImGui::TableHeadersRow();
            auto row = [&]( const char* InName, const Vector3& InPredicted, const Vector3& InServer, float InScale, const char* InUnit ) {
                const Vector3 delta = InServer - InPredicted;
                ImGui::TableNextRow();
                ImGui::TableNextColumn();
                ImGui::TextUnformatted( InName );
                ImGui::TableNextColumn();
                ImGui::Text( "%.4f, %.4f, %.4f", InPredicted.x, InPredicted.y, InPredicted.z );
                ImGui::TableNextColumn();
                ImGui::Text( "%.4f, %.4f, %.4f", InServer.x, InServer.y, InServer.z );
                ImGui::TableNextColumn();
                ImGui::Text( "%.1f %s", delta.Length() * InScale, InUnit );
            };
            row( "position", record.Predicted.Position, record.Authoritative.Position, 1000.f, "mm" );
            row( "velocity", record.Predicted.Velocity, record.Authoritative.Velocity, 1.f, "m/s" );
            row( "ground normal", record.Predicted.GroundNormal, record.Authoritative.GroundNormal, 1.f, "" );
            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            ImGui::TextUnformatted( "grounded" );
            ImGui::TableNextColumn();
            ImGui::TextUnformatted( record.Predicted.Grounded ? "yes" : "no" );
            ImGui::TableNextColumn();
            ImGui::TextUnformatted( record.Authoritative.Grounded ? "yes" : "no" );
            ImGui::TableNextColumn();
            ImGui::TextUnformatted( record.Predicted.Grounded == record.Authoritative.Grounded ? "" : "differs" );
            ImGui::EndTable();
        }
        for( const auto& [begin, end] : record.ScriptDiffs )
        {
            ImGui::Text( "[Predicted] script bytes %u-%u differ", begin, end - 1 );
        }
        ImGui::Text( "%zu ticks resimulated in %.3f ms; the present moved %.1f mm (the visual offset hides it)", record.Resimulated.size(), record.ResimMilliseconds,
            record.PresentJump.Length() * 1000.f );
        if( ImGui::Button( "Export" ) )
        {
            actions.Export = true;
            actions.ExportTick = record.Tick;
        }
        ImGui::SetItemTooltip( "Writes .tmp/Net/Mispredict_%u.json (the start state and the commands): NetMispredict::Run replays it offline", record.Tick );
        if( !InUI.LastExport.empty() )
        {
            ImGui::SameLine();
            ImGui::TextUnformatted( InUI.LastExport.c_str() );
        }
        if( ImGui::TreeNode( "Commands", "Commands (%zu, from tick %u)", record.Commands.size(), record.Tick ) )
        {
            for( const InputCommand& command : record.Commands )
            {
                ImGui::Text( "%u: buttons %08x, axes %.3f %.3f %.3f %.3f%s", command.Tick, command.Buttons, command.Axes[0], command.Axes[1], command.Axes[2], command.Axes[3],
                    command.HasAim ? "" : " (no aim)" );
            }
            ImGui::TreePop();
        }
    }
}
#endif


NetDebuggerActions DrawNetDebuggerView( const NetDebuggerView& InView, NetDebuggerUI& InUI, const NetDebuggerHistory* InHistory )
{
    NetDebuggerActions actions;
#if USING( ME_IMGUI )
    if( !InView.Client )
    {
        ImGui::TextUnformatted( InView.Reason.empty() ? "No client session." : InView.Reason.c_str() );
        return actions;
    }
    ImGui::Text( "Client #%u, tick %u (%s)", InView.ClientId, InView.Tick, InView.Connected ? "connected" : "not connected" );
    ImGui::Text( "RTT %.1f ms (+-%.1f), loss %.1f%%, in %.1f kbps, out %.1f kbps", InView.RoundTripMs, InView.RoundTripDeviationMs, InView.PacketLoss * 100.f, InView.KbpsIn,
        InView.KbpsOut );
    ImGui::Text( "Slack %.2f (target %.2f), dilation %+.2f%%, %d resyncs, %llu hitches", InView.Slack, InView.TargetSlack, InView.Dilation * 100.f, InView.Resyncs,
        static_cast<unsigned long long>( InView.Hitches ) );
    // The prediction first (its timeline is what the debugger is for), then the link's graphs.
    DrawPrediction( InView, InUI, actions );
    ImGui::Separator();
    if( InHistory && ImGui::CollapsingHeader( "Link graphs", ImGuiTreeNodeFlags_DefaultOpen ) )
    {
        DrawNetDebuggerGraphs( *InHistory );
    }
    // The blob channel (ME-323): the counts, and a bar for each blob on its way (a killcam arriving).
    ImGui::Text( "Blobs: %llu received, %llu sent (%llu acked), %llu cancelled (%llu by the server); slices %llu sent, %llu resent, %llu duplicate",
        static_cast<unsigned long long>( InView.BlobsReceived ), static_cast<unsigned long long>( InView.BlobsSent ), static_cast<unsigned long long>( InView.BlobsAcked ),
        static_cast<unsigned long long>( InView.BlobsCancelled ), static_cast<unsigned long long>( InView.BlobsCancelledByRemote ),
        static_cast<unsigned long long>( InView.BlobSlicesSent ), static_cast<unsigned long long>( InView.BlobSlicesResent ),
        static_cast<unsigned long long>( InView.BlobSlicesDuplicate ) );
    for( const NetBlobProgress& blob : InView.Blobs )
    {
        char label[128];
        std::snprintf( label, sizeof( label ), "%s #%u: %.1f / %.1f KB%s", blob.Outgoing ? "out" : "in", blob.Id, static_cast<double>( blob.DoneBytes ) / 1024.0,
            static_cast<double>( blob.Size ) / 1024.0, blob.Outgoing && !blob.Started ? " (queued)" : "" );
        ImGui::ProgressBar( blob.Size > 0 ? static_cast<float>( static_cast<double>( blob.DoneBytes ) / static_cast<double>( blob.Size ) ) : 0.f, ImVec2( -1.f, 0.f ), label );
    }

    // The world overlays (NetDebugDraw), drawn by whoever owns the session: each shot beside the
    // server's result for 2 s, the server ghost.
    if( ImGui::Checkbox( "World overlays (shots: client green, server blue / rewound yellow / live grey; server ghost blue)", &InUI.DrawOverlays ) )
    {
        actions.OverlaysChanged = true;
    }
    // Shots: the client's prediction against the server's lag-compensated result.
    if( ImGui::CollapsingHeader( "Shots", ImGuiTreeNodeFlags_DefaultOpen ) )
    {
        if( !InView.ShotsOn )
        {
            ImGui::TextDisabled( "(not compared: the server hasn't been asked)" );
        }
        ImGui::TextWrapped( "%s", InView.ShotsSummary.c_str() );
        if( !InView.Shots.empty() && ImGui::BeginTable( "##NetShots", 4, ImGuiTableFlags_Borders | ImGuiTableFlags_SizingStretchProp | ImGuiTableFlags_ScrollY,
                ImVec2( 0.f, ImGui::GetTextLineHeightWithSpacing() * 7.f ) ) )
        {
            ImGui::TableSetupColumn( "tick" );
            ImGui::TableSetupColumn( "view tick" );
            ImGui::TableSetupColumn( "rewound" );
            ImGui::TableSetupColumn( "client vs server" );
            ImGui::TableHeadersRow();
            for( auto it = InView.Shots.rbegin(); it != InView.Shots.rend(); ++it )
            {
                const NetDebuggerView::Shot& shot = *it;
                ImGui::TableNextRow();
                ImGui::TableNextColumn();
                ImGui::Text( "%u", shot.Tick );
                ImGui::TableNextColumn();
                ImGui::Text( "%.2f", shot.ViewTick );
                ImGui::TableNextColumn();
                if( shot.HasServer )
                {
                    ImGui::Text( "%.2f%s", shot.RewoundTicks, shot.Clamped ? " (cap)" : "" );
                }
                else
                {
                    ImGui::TextUnformatted( "-" );
                }
                ImGui::TableNextColumn();
                const ImVec4 color = shot.Verdict == 0 ? ImVec4( 0.7f, 0.7f, 0.7f, 1.f ) : shot.Verdict == 1 ? ImVec4( 0.4f, 0.9f, 0.4f, 1.f ) : ImVec4( 1.f, 0.35f, 0.3f, 1.f );
                ImGui::TextColored( color, "%s", shot.Describe.c_str() );
            }
            ImGui::EndTable();
        }
    }
    // The entities taking the most of this client's snapshot bytes (the server's writer counts them).
    if( ImGui::CollapsingHeader( "Top bandwidth", ImGuiTreeNodeFlags_DefaultOpen ) )
    {
        const NetBandwidthReport& report = InView.Bandwidth;
        if( report.Tick == 0 )
        {
            ImGui::TextUnformatted( InView.BandwidthOn ? "No report from the server yet (it sends one a second)." : "Not asked for." );
        }
        else
        {
            const double total = report.Seconds > 0.0 ? static_cast<double>( report.SnapshotBytes ) / report.Seconds : 0.0;
            ImGui::Text( "Snapshots %.2f KB/s over %.1f s to tick %u; headers %.0f%%", total / 1024.0, report.Seconds, report.Tick,
                report.SnapshotBytes > 0 ? 100.0 * static_cast<double>( report.OverheadBits ) / ( static_cast<double>( report.SnapshotBytes ) * 8.0 ) : 0.0 );
            for( const NetEntityBytes& entity : report.Top )
            {
                char label[160];
                std::snprintf( label, sizeof( label ), "#%u %s: %.0f B/s", entity.NetId, entity.Name.empty() ? "-" : entity.Name.c_str(), entity.BytesPerSecond );
                ImGui::ProgressBar( total > 0.0 ? static_cast<float>( entity.BytesPerSecond / total ) : 0.f, ImVec2( -1.f, 0.f ), label );
            }
        }
    }
#else
    (void)InView;
    (void)InUI;
    (void)InHistory;
#endif
    return actions;
}
