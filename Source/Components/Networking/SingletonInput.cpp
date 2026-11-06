#include "PCH.h"
#include "Components/Networking/SingletonInput.h"
#include "CLog.h"
#include "Engine/Input.h"
#include "Net/NetVersion.h"
#include <algorithm>
#include <atomic>
#include <cmath>

void SingletonInput::BuildCommandLayout( const Input& InInput )
{
    // One counter for every world: a C# cache keyed on the version never mistakes another world's
    // layout for its own.
    static std::atomic<uint32_t> s_layoutVersions{ 0 };
    m_commandSlots.clear();
    m_overrides = 0;
    m_layoutVersion = ++s_layoutVersions;
    m_layoutMapVersion = const_cast<Input&>( InInput ).GetActions().GetMapVersion();
    std::vector<std::pair<std::string, InputActionType>> actions;
    for( const InputContext& context : const_cast<Input&>( InInput ).GetActions().GetMap().Contexts )
    {
        for( const InputAction& action : context.Actions )
        {
            const bool known = std::any_of( actions.begin(), actions.end(), [&action]( const auto& InKnown ) { return InKnown.first == action.Name; } );
            if( action.Networked && !known )
            {
                actions.emplace_back( action.Name, action.Type );
            }
        }
    }
    std::sort( actions.begin(), actions.end() );
    int bit = 0;
    int axis = 0;
    uint64_t schema = NetVersion::Hash( "commands" );
    for( const auto& [name, type] : actions )
    {
        CommandSlot slot;
        slot.Action = name;
        slot.IsButton = type == InputActionType::Button;
        slot.AxisCount = type == InputActionType::Vector2 ? 2 : 1;
        if( slot.IsButton ? bit >= 32 : axis + slot.AxisCount > 4 )
        {
            CLog::Log( CLog::LogType::Warning, "Net: no room in the command for the networked action " + name + " (32 buttons, 4 axes)" );
            continue;
        }
        slot.Index = slot.IsButton ? bit++ : axis;
        if( !slot.IsButton )
        {
            axis += slot.AxisCount;
        }
        schema = NetVersion::Hash( name + ( slot.IsButton ? ":b\n" : ":a" + std::to_string( slot.AxisCount ) + "\n" ), schema );
        m_commandSlots.push_back( slot );
    }
    m_commandSchema = schema;
}


void SingletonInput::RefreshCommandLayout( const Input& InInput )
{
    if( m_layoutVersion == 0 || const_cast<Input&>( InInput ).GetActions().GetMapVersion() != m_layoutMapVersion )
    {
        BuildCommandLayout( InInput );
    }
}


void SingletonInput::SampleCommand( const Input& InInput, InputCommand& OutCommand ) const
{
    OutCommand.Buttons = m_overrides;
    OutCommand.Axes = {};
    // Quantized here, so the client predicts with exactly the aim the server will read.
    OutCommand.HasAim = m_hasAim;
    OutCommand.Yaw = m_hasAim ? NetCommands::QuantizeYaw( m_yaw ) : 0.f;
    OutCommand.Pitch = m_hasAim ? NetCommands::QuantizePitch( m_pitch ) : 0.f;
    for( const CommandSlot& slot : m_commandSlots )
    {
        const InputActionState& state = InInput.GetAction( slot.Action );
        if( slot.IsButton )
        {
            // Inside a fixed step WasPressed carries the edges since the last step: a tap shorter
            // than a tick still holds the bit for one tick.
            OutCommand.Buttons |= state.IsPressed() || state.WasPressed() ? ( 1u << slot.Index ) : 0u;
        }
        else if( slot.AxisCount == 2 )
        {
            OutCommand.Axes[slot.Index] = std::clamp( state.Value.x, -1.f, 1.f );
            OutCommand.Axes[slot.Index + 1] = std::clamp( state.Value.y, -1.f, 1.f );
        }
        else
        {
            OutCommand.Axes[slot.Index] = std::clamp( state.Value.x, -1.f, 1.f );
        }
    }
}


int SingletonInput::GetActionIndex( const std::string& InAction ) const
{
    for( const CommandSlot& slot : m_commandSlots )
    {
        if( slot.Action == InAction && slot.IsButton )
        {
            return slot.Index;
        }
    }
    return -1;
}


int SingletonInput::GetAxisIndex( const std::string& InAction, int* OutCount ) const
{
    for( const CommandSlot& slot : m_commandSlots )
    {
        if( slot.Action == InAction && !slot.IsButton )
        {
            if( OutCount )
            {
                *OutCount = slot.AxisCount;
            }
            return slot.Index;
        }
    }
    if( OutCount )
    {
        *OutCount = 0;
    }
    return -1;
}


void SingletonInput::RecordLocalCommand( const InputCommand& InCommand )
{
    m_previousLocalCommand = m_localCommand;
    m_localCommand = InCommand;
    m_localCommand.Tick = m_localTick++;
    // As the server would read it: offline play behaves like a session.
    for( float& axis : m_localCommand.Axes )
    {
        axis = NetCommands::QuantizeAxis( axis );
    }
    m_hasLocalCommand = true;
}


void SingletonInput::ClearLocalCommand()
{
    m_localCommand = InputCommand();
    m_previousLocalCommand = InputCommand();
    m_hasLocalCommand = false;
}


void SingletonInput::ResetLocalInput()
{
    m_overrides = 0;
    m_hasAim = false;
    m_yaw = 0.f;
    m_pitch = 0.f;
    ClearLocalCommand();
    m_localTick = 0;
}


void SingletonInput::SetActionOverride( int InIndex, bool InHeld )
{
    if( InIndex < 0 || InIndex >= 32 )
    {
        return;
    }
    const uint32_t bit = 1u << InIndex;
    m_overrides = InHeld ? ( m_overrides | bit ) : ( m_overrides & ~bit );
}


void SingletonInput::SetAim( float InYaw, float InPitch )
{
    if( !std::isfinite( InYaw ) || !std::isfinite( InPitch ) )
    {
        return;
    }
    m_hasAim = true;
    m_yaw = InYaw;
    m_pitch = std::clamp( InPitch, -90.f, 90.f );
}
