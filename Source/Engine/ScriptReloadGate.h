#pragma once

// Script hot reload pauses while a network session is live (ME-311).
//
// Under ME_TOOLS every process watches Assets/ and rebuilds its C# when a .cs changes. With a
// server and clients running on one machine (RunNetLocal, MatchSmoke) one edit started several
// `dotnet build`s at once, and the reload changed the scripts' schema hash, which the handshake
// checks: later joiners were turned away ("scripts") while the peers already in played on with
// the old scripts. So while this process's NetCore has a live session (NetCore::IsSessionLive:
// hosting, or a client connecting or connected) Engine::PollAssetChanges asks this gate before
// RequestReload, and Engine::Run holds a build that was already running when the session began
// instead of swapping it in (PollReload( false ): a failed one is still reported).
//
// It logs once when hot reload pauses for a session and once when it resumes. An edit made while
// paused isn't lost: when the session ends the scripts rebuild then. Without a session (the
// editor, offline play) every edit reloads as before. The editor's explicit "Rebuild && Reload
// Scripts" button isn't gated (RequestReload( true ) swaps in through the hold): asking for it is
// the user's call.
class ScriptReloadGate
{
public:
    // Every frame, before the frame's file changes: whether a session is live now. True when the
    // session just ended with an edit waiting (the caller requests the reload then).
    bool Update( bool InSessionLive );
    // A script changed on disk: true when it may rebuild now; false while paused (remembered
    // until the session ends).
    bool OnScriptChanged();
    bool IsPaused() const { return m_paused; }
    // An edit arrived while paused and hasn't been rebuilt yet.
    bool HasPendingReload() const { return m_pending; }

private:
    bool m_paused = false;
    bool m_pending = false;
};
