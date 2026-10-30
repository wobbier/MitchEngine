# Object and property sequences

Add an **Animation > Sequence** component to a game object and open **View > Sequencer**.
Select the object, or choose it in the player picker beside the transport. The window keeps that sequence open while
you select other objects to pose them with the inspector or gizmo.

**Clip... > Save as asset...** exports an embedded animation to a versioned `.sequence` JSON file.
The two examples now reference `Assets/Animations/CameraDollyZoom.sequence` and
`Assets/Animations/LostTempleDollyZoom.sequence`. Double-click an asset in the browser to open it;
**Create > Animation Sequence** creates an empty one. Assign an asset to a scene Sequence with
**Clip... > Assign asset to player...** or its inspector's Clip field. **Edit in Sequencer** opens the player and clip.

Assets own the name, frame rate, length and track/key data. They reference named binding slots
such as **Camera**, never scene entity GUIDs. Each player supplies its own slot-to-object bindings,
Play on Start, Loop and Speed. Bind each slot to the intended object; unbound slots do not animate.
Duplicating a player/prefab remaps the object bindings while sharing its asset.

**Clip... > Save clip** / Ctrl+S saves the focused asset. Asset undo/redo and dirty state are separate from
the scene's history; changing keys does not dirty scene data. Saving a scene writes the clip
reference and bindings, and does not save unsaved asset edits. Closing/switching a dirty clip,
loading a scene or quitting prompts to save/discard/cancel. Save As preserves a copy. Valid file
changes hot reload linked players; invalid reloads retain the last good clip. **Clip... > Reload from disk**
explicitly reloads the open document. Existing embedded scene animations still load.

The transport stays above a resizable timeline/inspector split. The **Track** tab edits the
selected track and its key values; **Player** contains timing, playback and scene bindings;
**Clips** contains the player's named animation library. Playback buttons reuse the engine's
existing Play/Pause/Stop icons. Track colors distinguish position, rotation, scale and camera
properties; selected keys are gold and the playhead is red. Hover **(?)** for timeline gestures.

1. Set the frame rate, length, loop, playback speed and Play on Start.
2. Add a track, choose its target object, component and property.
3. Set the playhead frame, pose the target, and click **Key Current Value**. This replaces a key
   already at that frame. Alternatively, edit the key's frame/value directly in the list.
4. Choose Linear, SmoothStep or Hold per key; it controls interpolation to the following key.
   Rotation values are edited in degrees and stored/interpolated as normalized quaternions along
   the shortest arc. Use the timeline diamonds to select and move keys.
5. Play or scrub the preview. The transport stop button, closing the window, disabling/removing the
   component, or entering Play restores the original values. Saving/autosaving writes the
   original scene while keeping the preview available. Timeline and key edits support undo/redo.

The timeline uses a custom ImGui keyframe canvas:

- Drag empty track space to box-select keys across tracks; Shift adds and Ctrl/Cmd-click toggles.
- Drag any selected diamond to move the entire selection with frame snapping. One drag is one
  undo step; Escape cancels. Group bounds clamp together, preserving relative timing. Moves into
  unselected keys are rejected as a group, so they cannot silently overwrite animation.
- Use left/right arrows or the nudge buttons for single-frame shifts, and Delete to remove keys.
  Ctrl/Cmd+A selects all keys while the Sequencer owns focus.
- Ctrl+C copies the selected keys; Ctrl+V pastes them with the earliest at the playhead, keeping
  their spacing and tracks (replacing keys on those frames; past the end or onto a locked track,
  nothing is pasted); Ctrl+D duplicates the selection just after its last key; Ctrl+X cuts. Each
  is one undo step. The clipboard survives switching clips, so keys copy between matching tracks.
- A dragged key snaps onto the playhead, event markers and camera cuts when released near one.
- Each track's legend has **M** (mute: the track's Enabled) and **L** (lock: its keys can't be
  selected, moved, deleted or pasted over). Locks are saved with the clip.
- Ctrl/Cmd+wheel zooms around the mouse, middle-drag pans, and **Fit** shows the clip.
  Drag the ruler to scrub, or double-click a key to seek to it. The key list edits exact values.

Tracks can animate writable, serializable float/double, Vector2/3/4 and quaternion fields,
including nested reflected structs. Transform Position/Rotation/Scale are local to the parent.
Camera examples include FieldOfView, OrthographicSize, Near/Far, ClearColor, Viewport and
ProceduralSky.CloudCoverage. Scalars use `Value.x`; vectors/quaternions use their xy/xyz/xyzw
components. Keys use float precision. Reflected scalar ranges and component property-change
callbacks apply when sampling. Unsupported fields and missing/inactive/disabled targets are
skipped. Arrays, discrete enum/bool tracks and blending between sequences are future work.

Player binding targets serialize as GUIDs and remap with prefabs/duplication. Runtime bindings resolve
on each sample, so removed components/entities cannot leave cached pointers behind. The engine
updates sequences after skeletal animation, before LateUpdate and rendering. Scripts can write
parameters before that stage; their LateUpdate runs afterward. Avoid having multiple sequences
or scripts drive the same property simultaneously.

```csharp
Sequence sequence = GetComponent<Sequence>();
sequence.Play();          // restart from the beginning
sequence.Pause();         // hold the current values
sequence.Play(false);     // resume
sequence.Seek(1.5f);      // seconds, clamped to the sequence's duration
sequence.Stop();          // rewind and apply frame zero
bool playing = sequence.IsPlaying;
float seconds = sequence.Time;
float duration = sequence.Duration;
```

In the **Clips** tab, use **Add Current Clip**, give the entry a unique name, and choose its
`.sequence` asset. Entries belong to the scene player; shared assets do not contain the library
or object identities. Clips should use consistent slot names so the same bindings work for all
of them. **Use as default** assigns the entry as the player's authored startup clip.

```csharp
Sequence sequence = GetComponent<Sequence>();
sequence.Bind("Camera", Entity); // optional: supply/rebind a target in code
bool started = sequence.Play("DollyZoom");
sequence.Pause();
sequence.Play("DollyZoom", false); // resume this clip
sequence.PlayAsset("Assets/Animations/CameraDollyZoom.sequence");
bool configured = sequence.HasClip("DollyZoom");
```

```cpp
auto& sequence = camera->GetComponent<Sequence>();
sequence.Clips.push_back( { "DollyZoom", "Assets/Animations/CameraDollyZoom.sequence" } );
sequence.SetBinding( "Camera", camera );
bool started = sequence.PlayClip( "DollyZoom" );
```

## Event markers (GAME-4)

The **Events** lane above the tracks holds named markers for gameplay: a door opening on a beat,
a line of dialogue, a camera shake. Double-click the lane (or **Events > Add Event at Playhead**)
to add one, drag a pennant to move it (snapping, Escape cancels), and edit it in the **Events**
tab: name, frame, **receiver** object, a text **Payload** and a **Number**. With no receiver, the
event goes to the player itself. In a clip asset a receiver is a binding slot like a track's
(choosing an object makes the slot, and each player binds it); the tab warns when the slot is
unbound on this player, or when the receiver has no script.

A marker's **Action** adds a cue that needs no script (every marker still sends its event):
**Play audio** plays the receiver's AudioSource, or a one-shot of a clip path (Payload) at volume
Number; **Stop audio** stops it; **Play animation state** plays the receiver's Animator state
(Payload, picked from its states) with a Number-second crossfade. Pennants are coloured by action;
the tab warns when the receiver lacks the AudioSource or Animator.

Runtime playback fires each marker once per crossing, for times in (last time, now]: at any frame
rate and on a single long hitch (up to 8 laps of a short loop at once). Simultaneous markers fire
in list order, which stays sorted by frame (a marker moved onto a frame fires after the ones
already there). Play from the start fires frame-0 markers; a marker on the final frame fires
before Finished. Seeking, Pause and editor previews never fire markers; a resume continues from
where playback stopped. Speed can't be negative, so there is no reverse playback to define.

Playback notifications go to the player's entity: **Started** (Play from the start, or a
restart), **Finished** (a non-looping sequence reaching its end), **Interrupted** (Stop, a
restart or another clip while playing) and **Looped** (between laps, after that lap's markers).
Pause sends nothing.

Natively each one is a `SequenceEvent` (`Listen<SequenceEvent>`), fired during `SequenceCore`'s
update. C# gets them before that frame's `OnUpdate` (the engine-event queue, like animation
markers): first the receiver's script, if it implements `ISequenceReceiver`, then the EventBus.

```csharp
public class Door : Script, ISequenceReceiver
{
    public void OnSequenceEvent(SequenceEvent e)
    {
        if (e.Kind == SequenceEventKind.Marker && e.Name == "Open") Open(e.Payload == "fast" ? e.Number : 1f);
    }
}

// Or from anywhere: this player's markers and notifications (gone with the script, or Dispose).
GetComponent<Sequence>().OnEvent(e => { if (e.Kind == SequenceEventKind.Finished) StartGameplay(); });
EventBus.Subscribe<SequenceEvent>(e => ...);   // every player's
```

`SequenceEvent` carries `Player`, `Receiver` (`Entity.Null` for an unbound slot), `Kind`, `Name`
(the marker's, or the sequence's for notifications), `Payload`, `Number`, `Frame` and `Clip`.
Markers are saved with the animation: in the scene for an embedded sequence, in the `.sequence`
asset (as `Markers`, receivers by slot) for a clip. Older assets without markers still load.
`SequenceEventsTest.lvl` + `SequenceEventFlows.edscript` author markers with `seq-add-marker`,
`seq-move-marker`, `seq-marker-receiver`, `seq-marker-action`, `seq-delete-marker` and
`seq-assert-marker`, then play and check what `SequenceEventProbe.cs` heard (and that a
PlayAnimation cue switched the tube's Animator).

## Camera cuts (GAME-4)

The **Cameras** lane below Events cuts the Game View between cameras: each cut names a camera,
and its shot lasts until the next cut (bars show the shots). Select a camera and double-click the
lane (or **Cameras > Cut to Selected Camera at Playhead**); drag a cut to move it. One cut per
frame: moves and adds onto an occupied frame are refused. The **Cameras** tab picks a cut's
camera (entities with a Camera component) and warns about an unbound slot. In a clip asset a cut
names its camera by binding slot (`Cuts` in the `.sequence`), so each player binds its own.

Cutting is part of sampling: a sample makes the active cut's camera current
(`Camera::SetCurrent`), so scrubbing and previews show the shot, and stopping a preview gives the
scene its camera back. Before the first cut the scene's current camera is left alone. Playback
owns the camera only while it runs: a finished (or paused) sequence holds its last shot until
gameplay changes cameras. `Sequence::GetCutCamera( frame )` answers which camera a frame uses.
Flows: `seq-add-cut frame | camera`, `seq-move-cut`, `seq-delete-cut`, `seq-assert-cut`,
`seq-assert-cut-count` and `seq-assert-cut-camera name` (the camera rendering now).

## Clip playback rules

Switching to a different asset starts it at frame zero, including when `restart` is false.
Requesting the same asset with `restart: false` resumes its current time. Missing/duplicate names,
empty paths and assets that cannot load return false without interrupting existing playback.
Named/direct asset playback does not replace the authored default clip reference. `Play()` and
`Play(false)` control the currently active clip; call `PlayAsset` with the default path to return
to it. Switching clips holds properties that the next clip does not drive; crossfades and restore
policies are future work. These are object/property clips, separate from imported skeletal clips.

Play on Start works in editor Play mode and standalone games. A nonlooping sequence holds its
final sample when it finishes; loops preserve overshoot. Runtime Pause/Stop do not restore the
editor preview snapshot; Stop samples frame zero. The playback clock and preview state are not
serialized. The Sequencer's editing UI is disabled during runtime playback.

`Assets/Scenes/Tests/SequenceTest.lvl` is a self-contained eight-second **dolly zoom**:
three seconds in, one second hold, three seconds out, one second hold. The cyan foreground subject
stays the same size on screen while the background changes perspective; a separate background
cube demonstrates rotation/scale tracks. Rebuild it with `python3 Tools/BuildSequenceTest.py`.
LostTemple's active Main Camera has a dolly zoom based on its current authored framing/focus plane;
press **P** during Play to toggle it, or preview it in the Sequencer without starting the game.
It pulls away from the nearby expedition leader and narrows FOV, then pushes back in and widens
FOV; focus distance follows the movement. The existing tour controls remain available after pausing.

For a dolly zoom, keep `subject depth * tan(FOV / 2)` constant. Camera FieldOfView keys expose
**Focal Length** and **Smooth Focal Length** interpolation: these interpolate `cot(FOV / 2)`
instead of degrees, matching Linear and SmoothStep position keys respectively. Choose endpoint
FOVs for the target depths and use matching timing/easing on both tracks. This holds subject
framing between keys too; opposite endpoint FOVs alone still cause the subject size to drift.
The generator `Tools/CameraSequences.py` calculates compensated endpoints from the subject depth.

Validation lives in `Tests/Source/SequenceTests.cpp` and
`Tests/Source/SequenceAssetTests.cpp`, `Tests/Source/SequenceCanvasTests.cpp`, and the managed
`Assets/Scripts/Tests/SequenceClipProbe.cs`, `Generators.Tests/SequenceEventTests.cs`, plus
`Assets/Scenes/Tests/SequenceFlows.edscript` and `SequenceEventFlows.edscript` (run via
`Tools/RunEditorFlows.py --only Sequence`).

The proposed next steps and C# event semantics are in [Sequencer roadmap](Sequencer-Roadmap.md).
