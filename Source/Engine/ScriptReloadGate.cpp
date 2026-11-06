#include "PCH.h"
#include "Engine/ScriptReloadGate.h"
#include "CLog.h"


bool ScriptReloadGate::Update( bool InSessionLive )
{
    if( InSessionLive == m_paused )
    {
        return false;
    }
    m_paused = InSessionLive;
    if( m_paused )
    {
        CLog::Log( CLog::LogType::Info, "Scripts: hot reload paused for the network session (edits rebuild once it ends)" );
        return false;
    }
    const bool rebuild = m_pending;
    m_pending = false;
    CLog::Log( CLog::LogType::Info, rebuild ? "Scripts: the network session ended; hot reload resumes, rebuilding the scripts edited during it"
                                            : "Scripts: the network session ended; hot reload resumes" );
    return rebuild;
}


bool ScriptReloadGate::OnScriptChanged()
{
    if( !m_paused )
    {
        return true;
    }
    if( !m_pending )
    {
        CLog::Log( CLog::LogType::Info, "Scripts: changed during the network session; not rebuilding until it ends" );
    }
    m_pending = true;
    return false;
}
