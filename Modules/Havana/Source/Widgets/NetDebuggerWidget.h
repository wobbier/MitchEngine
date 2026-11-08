#pragma once
#include <HavanaWidget.h>
#include "Dementia.h"

#if USING( ME_EDITOR )

#include "Engine/NetDebuggerView.h"
#include <cstdint>
#include <map>
#include <string>
#include <vector>

class EditorApp;

// Window > Net Debugger (NET-7): Play In Editor over the network in one dockable panel.
//
// Left, the session: the PIE settings and the link's condition sliders (NetPlay's, the same widgets
// as the toolbar's Net popup: live while playing), the child clients with Stop / Start / Restart,
// the listen server (its tick, the replay ring, the lag-compensation overlays in the Game View), a
// row per connection at the server's end (RTT, loss, kbps each way, the command slack the server
// measures, missed commands, the blobs it is sending: a killcam on its way) and the session's
// top-bandwidth entities (the snapshot writer's per-entity counts).
//
// Right, a tab per child: its own Net Debugger, then the server's end of its link as graphs. The
// panel asks each child for its view a few times a second over its --control-stdin ("net view
// [tick]", NetDebuggerView as JSON) and draws it with the Player's own code (DrawNetDebuggerView):
// the link, the misprediction timeline and the picked mark's field diff with Export (written by the
// child, under the project's .tmp/Net/), the link's graphs, the blobs arriving, the world-overlay
// toggle (drawn in the child's window), the shots against the server's results and its
// top-bandwidth report.
class NetDebuggerWidget
    : public HavanaWidget
{
public:
    explicit NetDebuggerWidget( EditorApp* InApp );
    ~NetDebuggerWidget();

    static NetDebuggerWidget* Get();

    void Init() final;
    void Destroy() final;
    void Update() final;
    void Render() final;

    // The newest view child InIndex answered with, and when it came (steady clock seconds); false
    // when none has (the panel closed, the child not running or not answering).
    bool GetClientView( int InIndex, NetDebuggerView& OutView, double& OutReceivedAt ) const;
    // The answer to the panel's last Export for child InIndex ("writing <path>", or why not) and the
    // tick it asked for; false before one came.
    bool GetLastExport( int InIndex, std::string& OutAnswer, uint32_t& OutTick ) const;
    // Seconds on the clock GetClientView's times are on.
    static double Now();

private:
    struct ClientState
    {
        int Pid = 0;                    // the child this is for (a restarted child starts afresh)
        NetDebuggerView View;
        bool HasView = false;
        double ReceivedAt = -1.0;
        std::string Error;
        uint64_t ViewId = 0;            // the view asked for, on its way: one at a time, however late
        double AskedAt = -1.0;
        uint64_t ExportId = 0;
        uint32_t ExportTick = 0;
        bool HasExport = false;         // an Export was answered (UI.LastExport)
        uint64_t OverlaysId = 0;
        bool OverlaysPending = false;   // UI.DrawOverlays still to send (toggled while one was on its way, or a restarted child)
        NetDebuggerUI UI;
        NetDebuggerHistory ClientHistory;   // the child's end, from its views
        NetDebuggerHistory ServerHistory;   // the server's end of its connection
        NetRateMeter ServerRate;
    };

    // Asks every running child for its view, takes the answers in.
    void Poll();
    void DrawSession();
    void DrawServer();
    void DrawClient( int InIndex, ClientState& InState );
    void DrawServerEnd( ClientState& InState );

    EditorApp* m_app = nullptr;
    std::map<int, ClientState> m_clients;
    double m_hostingSince = -1.0;
};

#endif
