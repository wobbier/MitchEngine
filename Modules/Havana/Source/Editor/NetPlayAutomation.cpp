#include "NetPlayAutomation.h"

#if USING( ME_EDITOR )

#include "CLog.h"
#include "Components/Networking/NetIdentity.h"
#include "Components/Physics/CharacterController.h"
#include "Cores/NetCore.h"
#include "EditorApp.h"
#include "Editor/NetPlay.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "JSON.h"
#include "Net/NetPrediction.h"
#include "Widgets/NetDebuggerWidget.h"
#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <memory>

namespace
{
    std::string Trim( const std::string& InText )
    {
        const size_t first = InText.find_first_not_of( " \t\r\n" );
        if( first == std::string::npos )
        {
            return {};
        }
        const size_t last = InText.find_last_not_of( " \t\r\n" );
        return InText.substr( first, last - first + 1 );
    }

    // "a | b | c" -> { a, b, c }.
    std::vector<std::string> SplitBars( const std::string& InText )
    {
        std::vector<std::string> parts;
        size_t start = 0;
        for( ;; )
        {
            const size_t bar = InText.find( '|', start );
            parts.push_back( Trim( InText.substr( start, bar == std::string::npos ? std::string::npos : bar - start ) ) );
            if( bar == std::string::npos )
            {
                break;
            }
            start = bar + 1;
        }
        return parts;
    }

    int FramesOr( const std::vector<std::string>& InParts, size_t InIndex, int InDefault )
    {
        return InParts.size() > InIndex && !InParts[InIndex].empty() ? std::max( 1, std::atoi( InParts[InIndex].c_str() ) ) : InDefault;
    }

    bool NearlyEqual( const json& InA, const json& InB, double InTolerance )
    {
        if( InA.is_number() && InB.is_number() )
        {
            return std::fabs( InA.get<double>() - InB.get<double>() ) <= InTolerance;
        }
        if( InA.is_array() && InB.is_array() )
        {
            if( InA.size() != InB.size() )
            {
                return false;
            }
            for( size_t i = 0; i < InA.size(); ++i )
            {
                if( !NearlyEqual( InA[i], InB[i], InTolerance ) )
                {
                    return false;
                }
            }
            return true;
        }
        return InA == InB;
    }

    // The tick the last net-knockback landed on (the server's), for assert-net-mispredict ... | knockback.
    int64_t s_knockbackTick = -1;

    // Asks child InIndex a console line and waits for its answer, re-asking every InEvery frames
    // while the answer doesn't satisfy InJudge (0 pending: ask again, 1 / -1 settled).
    struct Asker
    {
        int Index = 0;
        uint64_t Id = 0;
        int Wait = 0;
        std::string Detail = "no answer yet";
    };

    std::function<int( std::string& )> AskUntil( int InIndex, std::function<std::string()> InLine, std::function<int( const std::string&, std::string& )> InJudge, int InEvery = 10 )
    {
        auto asker = std::make_shared<Asker>();
        asker->Index = InIndex;
        return [asker, InLine, InJudge, InEvery]( std::string& OutDetail ) -> int {
            NetPlay& play = NetPlay::Get();
            if( asker->Id == 0 )
            {
                if( asker->Wait > 0 )
                {
                    --asker->Wait;
                    OutDetail = asker->Detail;
                    return 0;
                }
                asker->Id = play.SendControl( asker->Index, InLine() );
                if( asker->Id == 0 )
                {
                    OutDetail = "client " + std::to_string( asker->Index ) + " isn't running (or its stdin closed)";
                    return -1;
                }
            }
            std::string answer;
            if( !play.TakeReply( asker->Index, asker->Id, answer ) )
            {
                OutDetail = asker->Detail;
                return 0;
            }
            asker->Id = 0;
            const int verdict = InJudge( answer, asker->Detail );
            OutDetail = asker->Detail;
            if( verdict == 0 )
            {
                asker->Wait = InEvery;
            }
            return verdict;
        };
    }

    // The server's pawn for PIE client InIndex: a networked CharacterController its client owns.
    EntityHandle FindServerPawn( int InIndex, std::string& OutError )
    {
        const int clientId = NetPlay::Get().GetClientId( InIndex );
        if( clientId <= 0 )
        {
            OutError = "client " + std::to_string( InIndex ) + " hasn't connected";
            return {};
        }
        std::shared_ptr<World> world = GetEngine().GetWorld().lock();
        EntityHandle found;
        world->ForEachEntity( [&]( Entity& InEntity ) {
            if( !found && InEntity.HasComponent<NetIdentity>() && InEntity.HasComponent<CharacterController>() && InEntity.GetComponent<NetIdentity>().GetOwner() == clientId )
            {
                found = world->GetEntity( InEntity.GetId() );
            }
        } );
        if( !found )
        {
            OutError = "client " + std::to_string( InIndex ) + " (#" + std::to_string( clientId ) + ") has no pawn on the server (a networked CharacterController it owns)";
        }
        return found;
    }
}


bool NetPlayAutomation::Handles( const std::string& InCommand )
{
    return InCommand == "play-net" || InCommand == "wait-net-connected" || InCommand == "assert-net-field" || InCommand == "net-knockback"
        || InCommand == "assert-net-mispredict" || InCommand == "net-condition" || InCommand == "wait-net-rtt" || InCommand == "net-control"
        || InCommand == "wait-net-children" || InCommand == "net-ui-set" || InCommand == "net-ui-click" || InCommand == "wait-net-view"
        || InCommand == "wait-net-export";
}


bool NetPlayAutomation::Execute( EditorApp& InApp, const std::string& InCommand, const std::string& InArgs, Poll& OutPoll, std::string& OutError )
{
    NetPlay& play = NetPlay::Get();
    const std::vector<std::string> parts = SplitBars( InArgs );
    OutPoll = Poll();
    OutPoll.What = InCommand + " " + InArgs;

    if( InCommand == "play-net" )
    {
        // play-net 2 | --hero 0
        const int clients = std::atoi( parts[0].c_str() );
        if( clients <= 0 || clients > NetPlay::kMaxClients )
        {
            OutError = "play-net: 1 to " + std::to_string( NetPlay::kMaxClients ) + " clients";
            return false;
        }
        if( InApp.IsGameRunning() )
        {
            OutError = "play-net: already playing";
            return false;
        }
        play.SetPendingPlay( clients, parts.size() > 1 ? parts[1] : std::string() );
        InApp.Play();
        if( !play.IsHosting() )
        {
            play.SetPendingPlay( -1, "" );
            OutError = "play-net: didn't start: " + play.GetLastError();
            return false;
        }
        return true;
    }
    if( InCommand == "wait-net-connected" )
    {
        const size_t wanted = static_cast<size_t>( std::max( 0, std::atoi( parts[0].c_str() ) ) );
        OutPoll.Frames = FramesOr( parts, 1, 1800 );
        OutPoll.Check = [wanted]( std::string& OutDetail ) -> int {
            NetPlay& play = NetPlay::Get();
            const size_t connected = play.GetConnectedCount();
            OutDetail = std::to_string( connected ) + " connected" + ( play.IsHosting() ? "" : " (not hosting)" );
            // Each one known by its client id too (its "connected as client #N" line came).
            size_t named = 0;
            for( const NetPlay::ChildInfo& child : play.GetChildren() )
            {
                named += child.ClientId > 0 ? 1 : 0;
            }
            return connected >= wanted && named >= wanted ? 1 : 0;
        };
        return true;
    }
    if( InCommand == "assert-net-field" )
    {
        // assert-net-field 1 | GameMode | Deathmatch.ScoreToWin | 7 [| 600]
        if( parts.size() < 4 )
        {
            OutError = "assert-net-field: client | entity | Component.Field | value [| frames]";
            return false;
        }
        const int index = std::atoi( parts[0].c_str() );
        const std::string entity = parts[1];
        const std::string field = parts[2];
        std::string valueText = parts[3];
        const bool negate = valueText.rfind( "!=", 0 ) == 0;
        if( negate )
        {
            valueText = Trim( valueText.substr( 2 ) );
        }
        double tolerance = 1e-3;
        if( const size_t tilde = valueText.find( " ~ " ); tilde != std::string::npos )
        {
            tolerance = std::atof( valueText.c_str() + tilde + 3 );
            valueText = valueText.substr( 0, tilde );
        }
        const json expected = json::parse( valueText, nullptr, false );
        if( expected.is_discarded() )
        {
            OutError = "assert-net-field: the value isn't JSON: " + valueText;
            return false;
        }
        OutPoll.Frames = FramesOr( parts, 4, 600 );
        OutPoll.Check = AskUntil( index, [entity, field]() { return "net field " + json( entity ).dump() + " " + field; },
            [=]( const std::string& InAnswer, std::string& OutDetail ) -> int {
                const json answer = json::parse( InAnswer, nullptr, false );
                if( answer.is_discarded() || !answer.is_object() )
                {
                    OutDetail = "client " + std::to_string( index ) + " answered " + InAnswer;
                    return -1;
                }
                if( !answer.value( "ok", false ) )
                {
                    // Not there yet (a spawn on its way) or never: keep asking until the frames run out.
                    OutDetail = "client " + std::to_string( index ) + ": " + answer.value( "error", std::string( "?" ) );
                    return 0;
                }
                const json& actual = answer["value"];
                OutDetail = "client " + std::to_string( index ) + " has " + entity + " " + field + " = " + actual.dump() + ", expected " + ( negate ? "not " : "" ) + valueText;
                return NearlyEqual( actual, expected, tolerance ) != negate ? 1 : 0;
            } );
        return true;
    }
    if( InCommand == "net-knockback" )
    {
        // net-knockback 1 | [0, 6, 0]
        const json velocity = json::parse( parts.size() > 1 ? parts[1] : std::string(), nullptr, false );
        if( !velocity.is_array() || velocity.size() != 3 )
        {
            OutError = "net-knockback: client | [x, y, z] (a velocity, m/s)";
            return false;
        }
        NetCore* net = GetEngine().Net;
        NetServer* server = net && play.IsHosting() ? net->GetServer() : nullptr;
        if( !server )
        {
            OutError = "net-knockback: not hosting (play-net first)";
            return false;
        }
        std::string error;
        EntityHandle pawn = FindServerPawn( std::atoi( parts[0].c_str() ), error );
        if( !pawn )
        {
            OutError = "net-knockback: " + error;
            return false;
        }
        CharacterController& controller = pawn->GetComponent<CharacterController>();
        const Vector3 push( velocity[0].get<float>(), velocity[1].get<float>(), velocity[2].get<float>() );
        controller.SetVelocity( controller.GetVelocity() + push );
        // Between frames: the next fixed step simulates the server's current tick (NetServer counts
        // it up at EndTick), so that tick is the first whose state has the push.
        s_knockbackTick = server->GetTick();
        CLog::Log( CLog::LogType::Info, "PIE: knocked client " + parts[0] + "'s pawn " + pawn->GetName() + " by " + velocity.dump() + " m/s on the server at tick "
            + std::to_string( s_knockbackTick ) );
        return true;
    }
    if( InCommand == "assert-net-mispredict" )
    {
        // assert-net-mispredict 1 | 1 | knockback [| 900]
        if( parts.size() < 2 )
        {
            OutError = "assert-net-mispredict: client | count [| tick|knockback] [| frames]";
            return false;
        }
        const int index = std::atoi( parts[0].c_str() );
        const size_t count = static_cast<size_t>( std::max( 0, std::atoi( parts[1].c_str() ) ) );
        int64_t tick = -1;
        if( parts.size() > 2 && !parts[2].empty() )
        {
            if( parts[2] == "knockback" )
            {
                if( s_knockbackTick < 0 )
                {
                    OutError = "assert-net-mispredict: no net-knockback yet";
                    return false;
                }
                tick = s_knockbackTick;
            }
            else
            {
                tick = std::atoll( parts[2].c_str() );
            }
        }
        OutPoll.Frames = FramesOr( parts, 3, 900 );
        const int64_t from = tick < 0 ? -1 : std::max<int64_t>( 0, tick - kMispredictWindow );
        const int64_t to = tick < 0 ? -1 : tick + kMispredictWindow;
        OutPoll.Check = AskUntil( index, [from, to]() { return from < 0 ? std::string( "net timeline" ) : "net timeline " + std::to_string( from ) + " " + std::to_string( to ); },
            [=]( const std::string& InAnswer, std::string& OutDetail ) -> int {
                const json answer = json::parse( InAnswer, nullptr, false );
                if( answer.is_discarded() || !answer.is_object() )
                {
                    OutDetail = "client " + std::to_string( index ) + " answered " + InAnswer;
                    return -1;
                }
                if( !answer.value( "predicting", false ) || answer["newest"].is_null() )
                {
                    OutDetail = "client " + std::to_string( index ) + " isn't predicting a pawn yet";
                    return 0;
                }
                const int64_t newest = answer["newest"].get<int64_t>();
                if( to >= 0 && newest < to )
                {
                    OutDetail = "client " + std::to_string( index ) + " has reconciled to tick " + std::to_string( newest ) + ", waiting for " + std::to_string( to );
                    return 0;
                }
                const json& corrections = answer["corrections"];
                OutDetail = "client " + std::to_string( index ) + ": " + std::to_string( corrections.size() ) + " correction(s) " + corrections.dump()
                    + ( tick >= 0 ? " in ticks " + std::to_string( from ) + ".." + std::to_string( to ) : std::string( " on the timeline" ) ) + ", expected "
                    + std::to_string( count ) + ( tick >= 0 ? " at tick " + std::to_string( tick ) : std::string() );
                if( corrections.size() != count )
                {
                    return -1;
                }
                if( tick >= 0 )
                {
                    for( const json& at : corrections )
                    {
                        if( at.get<int64_t>() != tick )
                        {
                            return -1;
                        }
                    }
                    if( count > 0 )
                    {
                        // The mark's record: what differed (a push moves the pawn: a position delta).
                        bool moved = false;
                        for( const json& record : answer["records"] )
                        {
                            if( record.value( "tick", int64_t( -1 ) ) == tick )
                            {
                                moved = record.value( "positionMm", 0.0 ) > 0.0;
                                OutDetail += "; the record: " + record.value( "describe", std::string() );
                            }
                        }
                        if( !moved )
                        {
                            OutDetail += "; no record at the tick with a position delta";
                            return -1;
                        }
                    }
                }
                CLog::Log( CLog::LogType::Info, "PIE: " + OutDetail );
                return 1;
            } );
        return true;
    }
    if( InCommand == "net-condition" )
    {
        // net-condition lag 60 jitter 5 loss 2% dup 1%
        std::vector<std::string> words;
        for( const std::string& part : parts )
        {
            size_t start = 0;
            while( ( start = part.find_first_not_of( " \t", start ) ) != std::string::npos )
            {
                const size_t end = part.find_first_of( " \t", start );
                words.push_back( part.substr( start, end == std::string::npos ? std::string::npos : end - start ) );
                start = end == std::string::npos ? part.size() : end;
            }
        }
        if( words.empty() || words.size() % 2 != 0 )
        {
            OutError = "net-condition: lag|jitter|loss|dup <value> pairs";
            return false;
        }
        NetConditions conditions = play.GetSettings().Conditions;
        for( size_t i = 0; i + 1 < words.size(); i += 2 )
        {
            std::string value = words[i + 1];
            float scale = 1.f;
            if( !value.empty() && value.back() == '%' )
            {
                value.pop_back();
                scale = 0.01f;
            }
            const float number = std::max( 0.f, static_cast<float>( std::atof( value.c_str() ) ) );
            if( words[i] == "lag" ) conditions.LatencyMs = number;
            else if( words[i] == "jitter" ) conditions.JitterMs = number;
            else if( words[i] == "loss" ) conditions.Loss = std::min( 1.f, number * scale );
            else if( words[i] == "dup" ) conditions.Duplicate = std::min( 1.f, number * scale );
            else
            {
                OutError = "net-condition: unknown " + words[i] + " (lag, jitter, loss, dup)";
                return false;
            }
        }
        play.SetConditions( conditions );
        return true;
    }
    if( InCommand == "wait-net-rtt" )
    {
        // wait-net-rtt 1 | 110 200 [| 900]
        const int index = std::atoi( parts[0].c_str() );
        const std::string range = parts.size() > 1 ? parts[1] : std::string();
        char* end = nullptr;
        const float low = std::strtof( range.c_str(), &end );
        const float high = end && *end ? std::strtof( end, nullptr ) : 1e9f;
        OutPoll.Frames = FramesOr( parts, 2, 900 );
        OutPoll.Check = [index, low, high]( std::string& OutDetail ) -> int {
            const float rtt = NetPlay::Get().GetRoundTripMs( index );
            OutDetail = "the server's RTT to client " + std::to_string( index ) + " is " + ( rtt < 0.f ? std::string( "unknown" ) : std::to_string( rtt ) + " ms" )
                + ", expected " + std::to_string( low ) + ".." + std::to_string( high );
            if( rtt >= low && rtt <= high )
            {
                CLog::Log( CLog::LogType::Info, "PIE: " + OutDetail );
                return 1;
            }
            return 0;
        };
        return true;
    }
    if( InCommand == "net-control" )
    {
        // net-control 1 | net debugger on
        const int index = std::atoi( parts[0].c_str() );
        const std::string line = parts.size() > 1 ? parts[1] : std::string();
        OutPoll.Frames = FramesOr( parts, 2, 300 );
        OutPoll.Check = AskUntil( index, [line]() { return line; }, [index, line]( const std::string& InAnswer, std::string& OutDetail ) -> int {
            CLog::Log( CLog::LogType::Info, "PIE: client " + std::to_string( index ) + " > " + line + "\n" + InAnswer );
            OutDetail = InAnswer;
            return 1;
        } );
        return true;
    }
    if( InCommand == "wait-net-children" )
    {
        const int wanted = std::max( 0, std::atoi( parts[0].c_str() ) );
        OutPoll.Frames = FramesOr( parts, 1, 600 );
        OutPoll.Check = [wanted]( std::string& OutDetail ) -> int {
            const int alive = NetPlay::CountChildProcesses();
            OutDetail = std::to_string( alive ) + " PIE child process(es) alive (/proc)";
            if( alive == wanted )
            {
                CLog::Log( CLog::LogType::Info, "PIE: " + OutDetail );
                return 1;
            }
            return 0;
        };
        return true;
    }
    if( InCommand == "net-ui-set" || InCommand == "net-ui-click" )
    {
        // net-ui-set Lag (ms, each way) | 60 [| frames]    net-ui-click Restart##2 [| frames]
        // Queued for the widget with that label (NetPlay's Driven*): it has to be drawn and enabled
        // (the Net Debugger panel open, or the toolbar's popup) before the frames run out.
        const bool set = InCommand == "net-ui-set";
        if( parts[0].empty() || ( set && ( parts.size() < 2 || parts[1].empty() ) ) )
        {
            OutError = set ? "net-ui-set: label | value [| frames]" : "net-ui-click: label [| frames]";
            return false;
        }
        const std::string label = parts[0];
        play.QueueUi( label, set ? parts[1] : std::string() );
        OutPoll.Frames = FramesOr( parts, set ? 2 : 1, 300 );
        OutPoll.Check = [label]( std::string& OutDetail ) -> int {
            if( NetPlay::Get().IsUiQueued( label ) )
            {
                OutDetail = "nothing drew \"" + label + "\" enabled to take it (is the Net Debugger panel open?)";
                return 0;
            }
            return 1;
        };
        return true;
    }
    if( InCommand == "wait-net-view" )
    {
        // wait-net-view 1 | rtt | 110 400 [| frames]: the Net Debugger panel's newest view of client 1
        // (what the child measures itself, answered since this command began) has the value in range.
        static const char* kKeys = "rtt, loss (%), slack, dilation (%), kbpsIn, kbpsOut, corrections, marks, connected (0 / 1), clientId, shots, "
                                   "knockbackDetail (1: the record in full is a mark within kMispredictWindow ticks after the last net-knockback)";
        if( parts.size() < 3 )
        {
            OutError = std::string( "wait-net-view: client | key | min [max] [| frames]; keys: " ) + kKeys;
            return false;
        }
        const int index = std::atoi( parts[0].c_str() );
        const std::string key = parts[1];
        char* end = nullptr;
        const double low = std::strtod( parts[2].c_str(), &end );
        const double high = end && *end ? std::strtod( end, nullptr ) : 1e18;
        auto read = [key]( const NetDebuggerView& InView, double& OutValue ) -> bool {
            if( key == "rtt" ) OutValue = InView.RoundTripMs;
            else if( key == "loss" ) OutValue = InView.PacketLoss * 100.0;
            else if( key == "slack" ) OutValue = InView.Slack;
            else if( key == "dilation" ) OutValue = InView.Dilation * 100.0;
            else if( key == "kbpsIn" ) OutValue = InView.KbpsIn;
            else if( key == "kbpsOut" ) OutValue = InView.KbpsOut;
            else if( key == "corrections" ) OutValue = static_cast<double>( InView.CountOnTimeline( NetPredictOutcome::Correction ) );
            else if( key == "marks" ) OutValue = static_cast<double>( InView.Marks.size() );
            else if( key == "connected" ) OutValue = InView.Connected ? 1.0 : 0.0;
            else if( key == "clientId" ) OutValue = InView.ClientId;
            else if( key == "shots" ) OutValue = static_cast<double>( InView.Shots.size() );
            else if( key == "knockbackDetail" )
                OutValue = InView.HasDetail && s_knockbackTick >= 0 && static_cast<int64_t>( InView.Detail.Tick ) >= s_knockbackTick
                        && static_cast<int64_t>( InView.Detail.Tick ) <= s_knockbackTick + kMispredictWindow
                    ? 1.0 : 0.0;
            else return false;
            return true;
        };
        double probe = 0.0;
        if( !read( NetDebuggerView(), probe ) )
        {
            OutError = "wait-net-view: unknown key " + key + " (" + kKeys + ")";
            return false;
        }
        const double since = NetDebuggerWidget::Now();
        OutPoll.Frames = FramesOr( parts, 3, 900 );
        OutPoll.Check = [index, key, low, high, since, read]( std::string& OutDetail ) -> int {
            NetDebuggerWidget* widget = NetDebuggerWidget::Get();
            NetDebuggerView view;
            double at = -1.0;
            if( !widget || !widget->IsOpen )
            {
                OutDetail = "the Net Debugger panel isn't open (show-window Net Debugger)";
                return 0;
            }
            if( !widget->GetClientView( index, view, at ) || at < since )
            {
                OutDetail = "no view of client " + std::to_string( index ) + " since the command began";
                return 0;
            }
            double value = 0.0;
            read( view, value );
            OutDetail = "the Net Debugger's view of client " + std::to_string( index ) + " has " + key + " " + std::to_string( value ) + ", expected "
                + std::to_string( low ) + ".." + std::to_string( high );
            if( view.Client && value >= low && value <= high )
            {
                CLog::Log( CLog::LogType::Info, "PIE: " + OutDetail );
                return 1;
            }
            return 0;
        };
        return true;
    }
    if( InCommand == "wait-net-export" )
    {
        // wait-net-export 1 | knockback [| frames]: the panel's last Export for client 1 was answered
        // "writing <path>", and that file (written by the child on its background lane) loads as the
        // record of the tick it asked for: within kMispredictWindow ticks after the last net-knockback
        // (knockback) or that tick (a number).
        if( parts.size() < 2 || parts[1].empty() )
        {
            OutError = "wait-net-export: client | knockback|tick [| frames]";
            return false;
        }
        const int index = std::atoi( parts[0].c_str() );
        int64_t tick = -1;
        if( parts[1] == "knockback" )
        {
            if( s_knockbackTick < 0 )
            {
                OutError = "wait-net-export: no net-knockback yet";
                return false;
            }
        }
        else
        {
            tick = std::atoll( parts[1].c_str() );
        }
        const int64_t knockback = s_knockbackTick;
        OutPoll.Frames = FramesOr( parts, 2, 600 );
        OutPoll.Check = [index, tick, knockback]( std::string& OutDetail ) -> int {
            NetDebuggerWidget* widget = NetDebuggerWidget::Get();
            std::string answer;
            uint32_t asked = 0;
            if( !widget || !widget->GetLastExport( index, answer, asked ) )
            {
                OutDetail = "the Net Debugger panel has no Export answer from client " + std::to_string( index );
                return 0;
            }
            const std::string writing = "writing ";
            if( answer.rfind( writing, 0 ) != 0 )
            {
                OutDetail = "client " + std::to_string( index ) + " answered the Export with: " + answer;
                return -1;
            }
            const std::string path = answer.substr( writing.size() );
            NetMispredictRecord record;
            std::string error;
            std::error_code missing;
            if( !std::filesystem::exists( path, missing ) || !NetMispredict::Load( path, record, &error ) )
            {
                OutDetail = path + " doesn't load (yet): " + ( error.empty() ? std::string( "no such file" ) : error );
                return 0;
            }
            const int64_t at = record.Tick;
            const bool inRange = tick >= 0 ? at == tick : at >= knockback && at <= knockback + kMispredictWindow;
            OutDetail = path + " is the record of tick " + std::to_string( at ) + " (asked for " + std::to_string( asked ) + ", "
                + ( tick >= 0 ? "expected " + std::to_string( tick ) : "knockback at " + std::to_string( knockback ) ) + ")";
            if( at != static_cast<int64_t>( asked ) || !inRange )
            {
                return -1;
            }
            CLog::Log( CLog::LogType::Info, "PIE: exported " + OutDetail );
            return 1;
        };
        return true;
    }
    OutError = "unknown net command " + InCommand;
    return false;
}

#endif
