# Input

Games read input in one of two ways:

- **Actions** (recommended): named controls such as `Jump` or `Move`, defined in a `.inputactions` asset, grouped in contexts and rebindable at runtime.
- **Devices** (directly): the keyboard and mouse as before, plus gamepads.

Gamepads come from SDL's GameController API, with hotplug into four stable slots, deadzones and rumble. `Input` takes a device snapshot each frame and the `InputActionSystem` evaluates the action map against it. The window pump, config, and the game-vs-editor `Input` split are covered in `Docs/Platform-Window-Input-Config.md`.

> Verified against engine commit e0ca1f26, 2026-10-09 (overhaul Wave 4).

## Overview

| Concept | Type | Notes |
|---------|------|-------|
| Action map | `InputActionMap` (`.inputactions` JSON, reflected) | Contexts, plus stick and trigger deadzones |
| Context | `InputContext` | `Priority`, `EnabledByDefault`, `ConsumeInput`, a list of actions |
| Action | `InputAction` | `Button`, `Value` or `Vector2`, a `PressThreshold`, a list of bindings |
| Binding | `InputBinding` | One control, an `Axis` composite (Negative / Positive) or a `Vector2` composite (Up / Down / Left / Right). Optional `Modifier`, `Scale`, `InvertY` |
| Live state | `InputActionState` | `GetValue`, `GetVector2`, `IsPressed`, `WasPressed`, `WasReleased`, `HeldSeconds` |
| Evaluator | `InputActionSystem` | Contexts on and off, overrides, interactive rebinding, prompt strings |
| Pads | `Gamepads` singleton | SDL controllers in slots 0–3; fires `GamepadConnectionEvent` |
| Snapshot | `InputDeviceState` | Keys, mouse buttons, mouse delta and scroll, four `GamepadState`s |
| Players | `InputPlayers` (`Input::GetPlayers`) | Local multiplayer: one action system per player, device pairing, join on button press, `InputPlayerEvent` |

## Key Files

| Path | Role |
|------|------|
| `Source/Input/InputActions.h` / `.cpp` | Map data (reflected), control-path parsing, evaluation, consumption, rebinding, overrides, display strings |
| `Source/Input/InputTypes.h` | `GamepadButton`, `GamepadAxis`, `GamepadStick`, `GamepadState`, `InputDeviceState` |
| `Source/Input/Gamepads.h` / `.cpp` | SDL controller slots, snapshots, rumble, `GamepadConnectionEvent` |
| `Source/Input/InputPlayers.h` / `.cpp` | Local players: their action systems and devices, joining, `InputPlayerEvent` |
| `Source/Input/InputActionsAsset.h` / `.cpp` | `InputActionsResource` (hot reloads) and `InputActionsMetadata` (edited in the asset browser) |
| `Source/Engine/Input.h` / `.cpp` | The device snapshot, `GetAction`, `LoadActions`, the gamepad API |
| `Source/Window/SDLWindow.cpp` | Forwards controller hotplug events to `Gamepads` |
| `Tests/Source/InputTests.cpp` | Behavioural tests on injected snapshots |

## How It Works

### Control paths

| Device | Paths | Value |
|--------|-------|-------|
| Keyboard | `Keyboard/<SDL key name>`: `Keyboard/Space`, `Keyboard/W`, `Keyboard/Left Shift`, `Keyboard/Return` | 0 / 1 |
| Mouse buttons | `Mouse/Left`, `Mouse/Right`, `Mouse/Middle`, `Mouse/X1`, `Mouse/X2` | 0 / 1 |
| Mouse motion | `Mouse/Delta` (pixels this frame, +Y up), `Mouse/DeltaX`, `Mouse/DeltaY`, `Mouse/Scroll`, `Mouse/ScrollX`, `Mouse/ScrollY` | Vector2 or scalar |
| Gamepad buttons | `Gamepad/South` (Xbox A / Cross), `East`, `West`, `North`, `Back`, `Guide`, `Start`, `LeftStickPress`, `RightStickPress`, `LeftShoulder`, `RightShoulder`, `DPadUp/Down/Left/Right`, `Misc`, `Paddle1`–`4`, `Touchpad` | 0 / 1 |
| Gamepad sticks | `Gamepad/LeftStick`, `Gamepad/RightStick` (deadzoned, +Y up), with `X` / `Y` / `Up` / `Down` / `Left` / `Right` suffixes for one axis or a half axis | Vector2 or 0..1 |
| Gamepad analog | `Gamepad/LeftTrigger`, `Gamepad/RightTrigger` (deadzoned), `Gamepad/DPad` (normalized Vector2) | 0..1 / Vector2 |

Keys are SDL scancodes, so bindings are physical positions. `Keyboard/W` is the key in W's place on any layout. Unknown paths are rejected by `IsValidControl`, and `SetBindingOverride` refuses them.

### Each frame

`Input::Update` (game and editor instances alike):

1. **Snapshot.** It fills `InputDeviceState` from SDL: keyboard state, mouse buttons, the relative mouse motion (Y flipped to up), the scroll since last frame and every gamepad slot. While the instance isn't capturing (the editor owns the keyboard, or game input is paused), the snapshot is empty, so actions and the gamepad API read zero.
2. **Hot reload.** It re-applies the action asset if it reloaded.
3. **Evaluate.** `InputActionSystem::Update` walks the contexts from highest to lowest priority:
   - **Read.** A disabled context's actions read zero. Otherwise each binding is read. A binding reads zero if a higher context consumed one of its controls, or if its modifier isn't held. The strongest binding wins, by length for Vector2 and by absolute value otherwise.
   - **Edges.** `Pressed` means the value reached `PressThreshold`. `PressedThisFrame` and `ReleasedThisFrame` are the edges, and `HeldSeconds` accumulates while pressed.
   - **Consume.** After an enabled `ConsumeInput` context is read, every control it binds is hidden from the contexts below it.

Composites:

- **Axis:** `strength(Positive) - strength(Negative)`.
- **Vector2:** `(Right - Left, Up - Down)`, normalized when longer than 1, so diagonal WASD isn't faster.

Gamepad shaping:

- **Sticks:** a radial deadzone. Inside `StickDeadzone` reads 0. Beyond that the value rescales so `StickOuterDeadzone` and past read 1.
- **Triggers:** below `TriggerDeadzone` reads 0, and the rest rescales to 0..1.
- **Which pad:** "Gamepad/..." reads the strongest connected pad, or only the slot set with `SetGamepadFilter`.

`GetAction( name )` returns the action from the highest-priority enabled context that defines it. If no enabled context has it, it returns a zero state, and the same for unknown names. Actions with the same name in several contexts (`Back` in Menu, `Pause` in Gameplay) therefore resolve to whichever context is live.

### Rebinding and overrides

- **Interactive rebinding.** `StartInteractiveRebind( action, bindingIndex, part, done )` waits for the next newly actuated control: a key, a mouse button, a pad button, a trigger past halfway, or a stick pushed past halfway. It binds that control and calls `done( path )`; the cancel controls (default `Keyboard/Escape`) call `done( "" )` instead. A stick pushed for a whole-binding Vector2 action binds the whole stick. Actions stay idle until the rebind completes.
- **Composite parts.** The part names a composite piece: `"Up"`, `"Negative"`, and so on. `""` means the binding's main control.
- **Overrides.** `SetBindingOverride` sets one directly. Overrides are keyed `Context/Action/Index[/Part]`, survive `SetMap` and hot reloads, and round-trip through `SaveOverrides()` / `LoadOverrides( json )`. Where the JSON is stored is up to the game.
- **Prompts.** `GetBindingDisplay( action, index )` and `GetControlDisplay( path )` give strings such as "Space", "Left Mouse", "Gamepad South" and "W/A/S/D", overrides included.

### Gamepads

`Gamepads` holds up to four `SDL_GameController`s:

- **Hotplug.** `SDLWindow` forwards `SDL_CONTROLLERDEVICEADDED` / `REMOVED`, including the added events SDL sends at startup for pads already connected. A pad takes the first free slot and keeps it until it disconnects.
- **Events.** Each change logs and fires `GamepadConnectionEvent { Slot, Connected, Name }`.
- **Rumble.** `Input::Rumble( low, high, seconds, slot )` drives the motors, and `Gamepads::RumbleTriggers` drives trigger motors where the pad has them. `Input::Rumble` only works while that `Input` is capturing.
- **Order.** Buttons and axes follow SDL's order. Stick Y is flipped so up is +1.
- **Unattended runs.** Automated runs (`AutomationRunner::IsUnattendedRun`) don't initialize SDL's joystick, game-controller, haptic or sensor subsystems at all. SDL probes HID devices when those start, so automated runs leave the user's hardware alone, and no pads are seen.

### Local multiplayer

`Input::GetPlayers()` (`InputPlayers`) holds the players.

- **Player 0** is the default player: `GetAction( name )` and `GetActions()` read it. It hears the keyboard and mouse and any pad no other player owns, so single-player games are unaffected.
- **Other players** have their own copy of the action map, so their own context states and rebinds, and their own devices: `AddPlayer( slot, keyboardMouse )`, `SetDevices( player, keyboardMouse, slot )`. A slot can be a pad slot, `kAnyGamepad` (any unowned pad) or `kNoGamepad`. Each frame every player's system gets a device mask (`InputActionSystem::SetDevices`: the keyboard and mouse on or off, and a bitmask of pad slots), so "Gamepad/..." reads only that player's pads and the keyboard is never read twice.
- **Joining:** `EnableJoining( maxPlayers, button = South )`. A pad no player owns that presses the button joins: as player 0's own pad while player 0 has none, then as a new player, up to the maximum.
- **Events:** `InputPlayerEvent` reports `Joined`, `DeviceLost` (the pad was unplugged; the player keeps its slot) and `DeviceRegained` (a pad is back in that slot).
- **API:** `Input::GetAction( player, name )` reads a player's action, and `RumblePlayer( player, … )` rumbles its pad. Scripts use `Input.Player( n )`, `Input.PlayerCount` and `Input.SetJoining( max )`.
- **Map changes:** a new map or a hot reload goes to every player, with contexts and overrides kept.

### Loading and editing maps

- **Startup.** `Engine::InitGame` loads `ProjectSettings::InputActions` (default `Assets/Config/Input.inputactions`) into the game `Input` before `Game::OnInitialize`. A game can call `GetInput().LoadActions( path )` for another map.
- **Hot reload.** The file is an `InputActionsResource`, so edits on disk hot reload (tools builds). Context enable states and overrides are kept across the reload.
- **Asset browser.** Selecting a `.inputactions` file shows its contents, drawn by the reflection inspector, with a **Save** button. This is the generic data-asset path: `MetaBase::GetEditableType` / `GetEditableData` / `SaveEditableData`. **Create > Input Actions** makes a starter map with Move / Look / Jump.
- **Project Settings > Input.** Shows the map path, connected pads, and a live table of every action's value and bindings, useful for checking a pad or a binding. Note that the game `Input` reads nothing while the editor has keyboard focus.

### FlyingCameraCore

The scene-loaded fly camera reads the `Move` action when the map has one, and WASD otherwise. It also uses:

- **Rise / sink:** E / Q, or the shoulder buttons.
- **Sprint:** Left Shift, or the left stick press (× `SpeedModifier`).
- **Look:** right mouse drag, or the right stick when the map has `Look`.

## How to Extend

- **Read an action:** `GetEngine().GetInput().GetAction( "Jump" ).WasPressed()`, or `.GetVector2()` for movement. Missing actions read zero, so code works before the map defines them.
- **Game states:** `GetInput().GetActions().SetContextEnabled( "Menu", true )`. Give menus a higher priority and `ConsumeInput` so Escape and the d-pad stop reaching gameplay.
- **A new control kind:** extend `InputActionSystem::Parse` (the path), `Read` (the value), `FindActuatedControl` (rebind capture) and `GetControlDisplay`, then add a test with an injected `InputDeviceState`.

## Caveats & Fragility

- **Pads pair by slot.** A player owns a slot, so a pad that comes back in another slot (after other pads moved) isn't recognized as the same pad. Players can't share the keyboard (split keyboard), and `SetGamepadFilter` on a player's system is overwritten each frame by its devices.
- **Sub-frame taps are lost.** Keys and buttons are sampled once per frame, so a press and release between two frames never registers.
- **Rebinding doesn't check conflicts.** Binding a control already used by another action leaves both bound.
- **No gyro, touchpad positions, LEDs or haptic patterns.** Only buttons, sticks, triggers and simple rumble are exposed.
- **Edits made in the asset browser aren't undoable.** Data-asset edits write files directly on Save; they don't go through the scene undo stack.
- **Ultralight UI still only gets the left mouse button.** The runtime UI is being replaced, so it wasn't extended.

## Related Docs

- `Docs/Platform-Window-Input-Config.md`: the event pump, the game and editor `Input` instances, config
- `Docs/Editor-Havana.md`: Project Settings and the asset browser
- `Docs/Architecture.md`: unattended runs
