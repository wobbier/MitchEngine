#include "PCH.h"
#include "LoadingScreen.h"
#include "CLog.h"
#include "Cores/UI/RmlUiCore.h"
#include "Engine/Engine.h"
#include "Path.h"
#include "Resource/ResourceCache.h"
#include "World/SceneStreaming.h"
#include <algorithm>
#include <chrono>
#include <cmath>

namespace
{
    // Asset loads count as done once none are pending for this many frames in a row (a model
    // finishing can start its textures), or after the timeout.
    constexpr int kSettleFrames = 3;
    constexpr float kSettleTimeoutSeconds = 60.f;


    RmlUiCore* Ui()
    {
        return GetEngine().RmlUi;
    }


    std::string FileName( const std::string& InPath )
    {
        const size_t slash = InPath.find_last_of( "/\\" );
        const std::string file = InPath.substr( slash == std::string::npos ? 0 : slash + 1 );
        const size_t dot = file.find_last_of( '.' );
        return dot == std::string::npos ? file : file.substr( 0, dot );
    }
}


LoadingScreen& LoadingScreen::Get()
{
    static LoadingScreen instance;
    return instance;
}


const char* LoadingScreen::GetPhaseName( Phase InPhase )
{
    switch( InPhase )
    {
    case Phase::Reading: return "Reading";
    case Phase::Settling: return "Loading assets";
    case Phase::Holding: return "Ready";
    case Phase::Idle:
    default: return "Idle";
    }
}


bool LoadingScreen::LoadSceneAsync( const std::string& InScene, const std::string& InDocument )
{
    if( IsLoading() )
    {
        YIKES( "Loading: already loading " + m_scene );
        return false;
    }
    if( !Path( InScene ).Exists )
    {
        YIKES( "Loading: no scene " + InScene );
        return false;
    }
    m_scene = InScene;
    m_document = InDocument.empty() ? ProjectSettings::Get().LoadingDocument : InDocument == "none" ? std::string() : InDocument;
    m_phase = Phase::Reading;
    m_elapsed = 0.f;
    m_settledFrames = 0;
    m_peakPending = 0;
    m_loggedPercent = -1;
    m_reading = std::async( std::launch::async, [scene = InScene]() { return SceneStreaming::ReadScene( scene ); } );

#if USING( ME_RMLUI )
    if( RmlUiCore* ui = Ui() )
    {
        ui->ModelEnsure( kModel );
        ui->ModelSetString( kModel, "scene", FileName( InScene ) );
        ui->ModelSetString( kModel, "product", ProjectSettings::Get().ProductName );
        if( !m_document.empty() )
        {
            ui->ShowOverlay( m_document );
        }
    }
#endif
    SetProgress( 0.f );
    CLog::Log( CLog::LogType::Info, "Loading: " + InScene + ( m_document.empty() ? "" : " (" + m_document + ")" ) );
    return true;
}


void LoadingScreen::PlaySplash( const std::vector<ProjectSettings::SplashScreen>& InScreens )
{
    m_splash = InScreens;
    m_splashIndex = 0;
    m_splashElapsed = 0.f;
#if USING( ME_RMLUI )
    if( RmlUiCore* ui = Ui() )
    {
        ui->ModelEnsure( kModel );
        ui->ModelSetString( kModel, "product", ProjectSettings::Get().ProductName );
        if( IsShowingSplash() )
        {
            ui->ShowOverlay( m_splash.front().Document, 2000 );
            CLog::Log( CLog::LogType::Info, "Splash: " + m_splash.front().Document );
        }
    }
#endif
}


void LoadingScreen::SetProgress( float InProgress )
{
    // With a minimum time the bar fills across it, never ahead of the real progress.
    const float paced = MinimumSeconds > 0.f && m_phase != Phase::Idle ? m_elapsed / MinimumSeconds : 1.f;
    m_progress = std::clamp( std::min( InProgress, paced ), 0.f, 1.f );
    const int percent = static_cast<int>( std::lround( m_progress * 100.f ) );
#if USING( ME_RMLUI )
    if( RmlUiCore* ui = Ui() )
    {
        ui->ModelSetNumber( kModel, "progress", m_progress );
        ui->ModelSetNumber( kModel, "percent", percent );
        ui->ModelSetString( kModel, "width", std::to_string( percent ) + "%" );
        ui->ModelSetString( kModel, "phase", GetPhaseName( m_phase ) );
    }
#endif
    // A line per quarter (the milestone it passed), so logs and flows can follow a load.
    if( percent / 25 != m_loggedPercent / 25 || m_loggedPercent < 0 )
    {
        m_loggedPercent = percent;
        CLog::Log( CLog::LogType::Info, "Loading: " + m_scene + " " + std::to_string( percent / 25 * 25 ) + "% (" + GetPhaseName( m_phase ) + ")" );
    }
}


void LoadingScreen::Finish()
{
#if USING( ME_RMLUI )
    if( RmlUiCore* ui = Ui(); ui && !m_document.empty() )
    {
        ui->HideOverlay( m_document );
    }
#endif
    CLog::Log( CLog::LogType::Info, "Loading: " + m_scene + " done" );
    m_phase = Phase::Idle;
    m_progress = 1.f;
}


void LoadingScreen::Update( float InUnscaledDeltaSeconds )
{
    if( IsShowingSplash() )
    {
        m_splashElapsed += InUnscaledDeltaSeconds;
        if( m_splashElapsed >= m_splash[m_splashIndex].Seconds )
        {
#if USING( ME_RMLUI )
            RmlUiCore* ui = Ui();
            if( ui )
            {
                ui->HideOverlay( m_splash[m_splashIndex].Document );
            }
            m_splashElapsed = 0.f;
            if( ++m_splashIndex < m_splash.size() && ui )
            {
                ui->ShowOverlay( m_splash[m_splashIndex].Document, 2000 );
                CLog::Log( CLog::LogType::Info, "Splash: " + m_splash[m_splashIndex].Document );
            }
#else
            ++m_splashIndex;
#endif
        }
    }

    if( m_phase == Phase::Idle )
    {
        return;
    }
    m_elapsed += InUnscaledDeltaSeconds;
    if( m_phase == Phase::Reading )
    {
        // Reading has no measurable steps: it creeps towards the half-way mark.
        SetProgress( 0.45f * ( 1.f - std::exp( -m_elapsed * 2.f ) ) );
        if( m_reading.wait_for( std::chrono::seconds( 0 ) ) != std::future_status::ready )
        {
            return;
        }
        const json data = m_reading.get();
        if( data.is_null() )
        {
            YIKES( "Loading: couldn't read " + m_scene );
            Finish();
            return;
        }
        GetEngine().LoadSceneFromData( data, m_scene );
        m_phase = Phase::Settling;
        SetProgress( 0.5f );
        return;
    }
    if( m_phase == Phase::Settling )
    {
        const size_t pending = ResourceCache::GetInstance().GetPendingLoadCount();
        m_peakPending = std::max( m_peakPending, pending );
        SetProgress( 0.5f + 0.5f * ( m_peakPending == 0 ? 1.f : 1.f - static_cast<float>( pending ) / static_cast<float>( m_peakPending ) ) * ( pending == 0 ? 1.f : 0.99f ) );
        m_settledFrames = pending == 0 ? m_settledFrames + 1 : 0;
        if( m_settledFrames < kSettleFrames && m_elapsed < kSettleTimeoutSeconds )
        {
            return;
        }
        m_phase = Phase::Holding;
        SetProgress( 1.f );
    }
    if( m_phase == Phase::Holding )
    {
        SetProgress( 1.f );
        if( m_elapsed >= MinimumSeconds )
        {
            Finish();
        }
    }
}
