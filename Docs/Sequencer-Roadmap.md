# Sequencer direction

Use a custom ImGui keyframe canvas, retaining ImGuizmo for scene transforms and the vendored
ImSequencer for other range-based tools if useful. The upstream
[ImSequencer interface](https://github.com/CedricGuillemet/ImGuizmo/blob/master/src/ImSequencer.h)
exposes item start/end ranges and a single selected entry. Its custom drawing callbacks are
useful, but per-key selection, grouping, key hit testing and multi-track operations still need
our own model. Owning that interaction code avoids coupling clip data and editing to range bars.
This is an engineering judgment based on the installed source/API, not a claim that it cannot
be extended.

The current foundation has reusable assets with scene bindings, named clip libraries callable
from C# and native code, independent asset undo/save,
scrubbing and restorable preview, dolly zoom interpolation, marquee selection, group movement,
collision protection, snapping, deletion, nudging, zoom and pan. The next milestones should be:

1. **Complete key editing.** *Partly done (GAME-4): copy/cut/paste at the playhead, duplicate,
   mute/lock, snapping to the playhead/markers/cuts. Still open: Alt-drag duplicate, solo, track
   groups, multi-key value/interpolation edits, selection framing, ripple/time scaling.* Copy/paste at the playhead, duplicate/Alt-drag, selection-preserving
   undo, key/track identities that survive reordering, multi-key value/interpolation edits,
   track groups, mute/solo/lock, selection framing and snapping to playhead/markers/other keys.
   Add ripple/time scaling as explicit tools with a visible preview of affected keys.
2. **Curves and recording.** Expand vector tracks into XYZ channels; add a graph editor with
   auto/clamped/user tangents and overshoot controls. Auto-key should capture inspector/gizmo
   changes only when explicitly armed, and a recording take should create one undo operation.
   Draw motion paths and camera frustums in the scene. Add relative-to-start/additive transform
   modes so a clip can play at another object's starting pose.
3. **C# event markers.** *Done (GAME-4): see [Sequences](Sequences.md#event-markers-game-4). The
   original plan follows.* Add a dedicated event lane containing a receiver binding, event name and
   typed payload. Prefer a typed `OnSequenceEvent(SequenceEvent e)` callback / registered handlers
   with editor validation over arbitrary method-name invocation. Fire events crossed during
   runtime playback exactly once per crossing, including low FPS and loop wrap; define ordering
   for simultaneous markers and explicit behavior for reverse playback. Seeking and editor
   preview should not invoke gameplay events by default. Also expose Started, Finished,
   Interrupted and Looped notifications for gameplay orchestration. These are proposed APIs,
   not implemented callbacks.
4. **Cinematic composition.** *Camera cuts done, and audio / animation-state cues as marker actions (GAME-4, see
   [Sequences](Sequences.md#camera-cuts-game-4)); waveforms and clip-length audio/animation bars are not.*
   Add camera-cut and shot tracks, audio clips/waveforms, nested
   sequences, play ranges/markers, crossfades, constraints/look-at targets and animation-clip
   tracks for actors. Keep playback deterministic at a requested time for captures and tools.
5. **Ownership and production behavior.** Specify hold-final versus restore-on-stop/interruption,
   conflict rules when gameplay and a sequence drive the same property, binding/type diagnostics,
   hot-reload conflict handling for dirty documents, and asset recovery/autosave. Establish track
   and key-count performance budgets and cull offscreen lanes before growing to large cutscenes.

Events and clips must remain separate from scene references: a reusable **OpenDoor** marker binds
to a particular receiver on the player. Event dispatch needs its own timeline-crossing tests;
sampling a property at a time must remain side-effect free apart from applying its value.
