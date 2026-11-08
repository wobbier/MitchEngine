#pragma once
#include "Dementia.h"
#if USING( ME_EDITOR )
#include <functional>
#include <string>

class EditorApp;

// The edscript's network commands (NET-7 PIE, NetPlay.h), for EditorAutomation. Clients are named
// by their PIE index (1..N, in start order), not the server's client id.
//
//   play-net N [| client args]           Play with a listen server and N child clients
//   wait-net-connected N [| frames]      until the listen server has accepted N clients (1800)
//   assert-net-field C | entity | Component.Field | value [| frames]
//                                        until client C's world has the value (assert-field's forms:
//                                        "!= v", "v ~ tol"; a C++ component's saved fields or a C#
//                                        script's public fields by class name; 600 frames)
//   net-knockback C | [x, y, z]          adds that velocity to client C's pawn on the server only
//                                        (its CharacterController): a server-only push the client
//                                        couldn't predict. Lands on the server's next tick
//   assert-net-mispredict C | N [| tick|knockback] [| frames]
//                                        client C's misprediction timeline has exactly N corrections:
//                                        in the whole timeline, or within kMispredictWindow ticks of
//                                        the tick (knockback: the last net-knockback's), each at that
//                                        tick with a position delta; waits until the client has
//                                        reconciled past the window (900 frames)
//   net-condition lag|jitter|loss|dup V [...]   the listen server's link, both directions, live
//   wait-net-rtt C | min [max] [| frames]       until the server's RTT to client C is in range (900)
//   net-control C | console line [| frames]     runs a console line on client C, logs its answer
//   wait-net-children N [| frames]       until N of this editor's PIE child processes are alive
//                                        (found in /proc, so orphans count; 600)
//   net-ui-set label | value [| frames]  a PIE widget (NetPlay's Driven*: the condition sliders,
//   net-ui-click label [| frames]        Clients, the children's Stop##N / Start##N / Restart##N,
//                                        "Lag-compensation overlays", the panel's "Export N") set or
//                                        clicked by its ImGui label, as the user would; fails unless
//                                        it is drawn and enabled within the frames (300)
//   wait-net-view C | key | min [max] [| frames]
//                                        the Net Debugger panel's newest view of client C (what the
//                                        child measures itself; one answered since the command began)
//                                        has key in range: rtt, loss, slack, dilation, kbpsIn, kbpsOut,
//                                        corrections, marks, connected, clientId, shots,
//                                        knockbackDetail (1 when its record in full is a mark within
//                                        kMispredictWindow ticks after the last net-knockback) (900)
//   wait-net-export C | knockback|tick [| frames]
//                                        the panel's last Export for client C was answered "writing
//                                        <path>" and that file loads (NetMispredict::Load) as the
//                                        record of the tick it asked for, after the last net-knockback
//                                        (within kMispredictWindow) or that tick (600)
namespace NetPlayAutomation
{
    constexpr int kMispredictWindow = 60;

    // A command that waits: Check runs once a frame until it returns 1 (passed) or -1 (failed, with
    // the detail), or Frames run out (failed with the last detail).
    struct Poll
    {
        std::function<int( std::string& OutDetail )> Check;
        int Frames = 0;
        std::string What;
    };

    bool Handles( const std::string& InCommand );
    // False with OutError when the command failed at once; a waiting command fills OutPoll.
    bool Execute( EditorApp& InApp, const std::string& InCommand, const std::string& InArgs, Poll& OutPoll, std::string& OutError );
}
#endif
