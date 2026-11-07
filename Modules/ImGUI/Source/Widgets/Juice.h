#pragma once
#include <imgui.h>
#include <functional>

// Juice (UX-3): the feedback that makes widgets feel alive. Springs with overshoot for values
// that move, a flash when a value changes, a tick and a little burst when a scrub crosses a
// detent, a shake when an entry is refused, and editor sound cues. Everything scales with the
// juice level (Appearance.Juice: Off / Subtle / Full, Full by default) and reduce motion
// (Appearance.ReduceMotion) turns motion off while keeping colour feedback. At Off the widgets
// are visually static: springs land at once and nothing flashes, bursts or shakes.
//
// State is keyed by ImGui ID and forgotten when an ID isn't touched for a second. Bursts are
// drawn on the foreground draw list of the viewport they were spawned in, after every window,
// by a context hook installed on first use.
namespace MeWidgets::Juice
{
    enum class Level : int
    {
        Off = 0,
        Subtle,
        Full,
    };

    void SetLevel( Level InLevel );
    Level GetLevel();
    void SetReduceMotion( bool InReduce );
    bool GetReduceMotion();
    // 0 (Off), 0.5 (Subtle) or 1 (Full): how strong colour feedback is.
    float Strength();
    // The same for motion: 0 when reduce motion is on.
    float Motion();

    // A spring towards InTarget that remembers its velocity per ID. Overshoots a little at Full,
    // settles critically at Subtle, and lands at once at Off or with reduce motion.
    float Spring( ImGuiID InId, float InTarget, float InFrequency = 9.f );
    // Puts the spring at InTarget with no motion (a value that changed without an edit here:
    // another entity selected, undo, a running game).
    void Snap( ImGuiID InId, float InTarget );
    // Whether the spring for InId is still moving (widgets keep redrawing while it is).
    bool IsMoving( ImGuiID InId );

    // A value changed (typed, reset, pasted): Flash() returns a decaying 0..1 glow for a moment.
    void Flash( ImGuiID InId );
    float FlashAmount( ImGuiID InId );

    // An entry was refused: a short decaying horizontal shake, in pixels.
    void Shake( ImGuiID InId );
    float ShakeOffset( ImGuiID InId );

    // A scrub crossed a detent (a round step, a range end): a tick mark that fades, a small
    // burst of sparks at InPos, and the Tick sound cue.
    void Tick( ImGuiID InId, ImVec2 InPos );
    float TickAmount( ImGuiID InId );

    // Sparks flying out from InPos in InColor (no-op at Off or with reduce motion).
    void Burst( ImVec2 InPos, ImU32 InColor, int InCount = 10 );
    int LiveParticles();

    // Editor sound cues. Silent unless the editor installs a player and turns sounds on.
    enum class Cue
    {
        Tick,
        Reset,
        Commit,
        Refuse,
    };
    void SetSoundPlayer( std::function<void( Cue )> InPlayer );
    void Play( Cue InCue );

    // Steps every spring, flash and particle by InDeltaTime and draws the bursts. Called by the
    // context hook each frame; tests call it directly with a fixed step.
    void Update( float InDeltaTime );
    void DrawParticles();
    // A fixed step per frame instead of ImGui's measured delta (runs with --frame-time, so
    // captures of an animation are the same on every machine). 0 measures again.
    void SetFixedDelta( float InSeconds );
    // Forget everything (tests, level changes).
    void Reset();
}
