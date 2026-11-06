#pragma once
#include "ECS/Component.h"
#include "Net/NetCommands.h"
#include <cstdint>
#include <string>
#include <vector>

class Input;

// A world's sampled input (NET-16, ME-297), as a world singleton: World::Singleton<SingletonInput>().
//
// - The networked actions' command layout (NET-6, InputAction::Networked), sorted by name: buttons
//   take the command's bits in that order, Value / Vector2 actions its 4 axes.
// - The local player's command for the fixed step being simulated (C#'s Net.LocalCommand), what
//   scripts hold (Net.Press) and the aim they set (Net.SetAim).
//
// The engine builds the layout into its game world's and samples the command there every fixed
// step; C#'s Net reads and writes the script world's. A second world (a killcam or replay world, a
// test) has its own, so nothing of one world's input reaches the other. The physical devices
// (Input) stay the engine's: a layout is built from an Input, and the command sampled from one.
class SingletonInput final
    : public Component<SingletonInput>
{
public:
    SingletonInput()
        : Component( "SingletonInput" )
    {
    }

    struct CommandSlot
    {
        std::string Action;
        int Index = 0;          // button bit, or first axis
        bool IsButton = true;
        int AxisCount = 0;      // 1 (Value) or 2 (Vector2)
    };

    // Builds the layout from InInput's action map (before a session starts: the handshake hashes it).
    void BuildCommandLayout( const Input& InInput );
    // Without a session: builds the layout again if the action map changed since (a hot reload,
    // the editor loading another map). A session's layout is fixed.
    void RefreshCommandLayout( const Input& InInput );
    // Changes whenever a layout is built, and no two builds share one, in any world (scripts cache
    // action indices against it, whichever world they run in). 0: never built.
    uint32_t GetLayoutVersion() const { return m_layoutVersion; }
    const std::vector<CommandSlot>& GetCommandSlots() const { return m_commandSlots; }
    // The layout as a hash, for the handshake.
    uint64_t GetCommandSchema() const { return m_commandSchema; }
    // Whether any action is networked (or scripts hold or aim): then the engine samples the local
    // command every tick (otherwise a C++ game's own NetCore::SetLocalCommand stands).
    bool HasCommandLayout() const { return !m_commandSlots.empty() || m_overrides != 0 || m_hasAim; }

    // The local player's command for the coming tick from InInput, plus whatever scripts hold.
    void SampleCommand( const Input& InInput, InputCommand& OutCommand ) const;
    // An action's button bit, or -1 (not networked, or not a button).
    int GetActionIndex( const std::string& InAction ) const;
    // A networked Value / Vector2 action's first axis, or -1 (not networked, or a button);
    // OutCount: its axes (1 or 2).
    int GetAxisIndex( const std::string& InAction, int* OutCount = nullptr ) const;

    // The local player's command for the fixed step being simulated, as the engine sampled it:
    // recorded every step while HasCommandLayout(), offline and on a client, with its axes as the
    // server would read them. Offline, Tick counts the steps.
    void RecordLocalCommand( const InputCommand& InCommand );
    // A step that samples nothing (no layout, or the server): Net.LocalCommand reads invalid, not
    // the last command recorded.
    void ClearLocalCommand();
    bool HasLocalCommand() const { return m_hasLocalCommand; }
    const InputCommand& GetLocalCommand() const { return m_localCommand; }
    const InputCommand& GetPreviousLocalCommand() const { return m_previousLocalCommand; }

    // Net.Press: a button held by a script until released.
    void SetActionOverride( int InIndex, bool InHeld );
    // The local player's absolute aim (NET-15, C#'s Net.SetAim), sent with every command until
    // cleared: degrees, InputCommand's convention.
    void SetAim( float InYaw, float InPitch );
    void ClearAim() { m_hasAim = false; }
    // The editor's Stop: what scripts held and aimed, and the recorded local commands, go with the
    // play session, so the next Play starts with none. The layout stays (scripts' cached indices
    // are keyed on its version).
    void ResetLocalInput();

private:
    std::vector<CommandSlot> m_commandSlots;
    uint32_t m_layoutVersion = 0;
    uint32_t m_layoutMapVersion = 0;    // the action map's version it was built from
    uint64_t m_commandSchema = 0;
    InputCommand m_localCommand;
    InputCommand m_previousLocalCommand;
    uint32_t m_localTick = 0;
    bool m_hasLocalCommand = false;
    uint32_t m_overrides = 0;
    bool m_hasAim = false;
    float m_yaw = 0.f;
    float m_pitch = 0.f;
};
