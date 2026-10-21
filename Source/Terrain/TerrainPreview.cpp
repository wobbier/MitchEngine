#include "PCH.h"
#include "TerrainPreview.h"
#include <chrono>

namespace TerrainData
{
    TerrainPreview::TerrainPreview( Builder builder ) : m_builder( std::move( builder ) ) {}


    TerrainPreview::~TerrainPreview()
    {
        Reset();
        if( m_job.valid() ) m_job.wait();
    }


    void TerrainPreview::Reset()
    {
        ++m_epoch; m_requested = false;
        if( m_cancel ) m_cancel->store( true, std::memory_order_relaxed );
    }


    bool TerrainPreview::Request( const Heightfield& field, uint64_t guid, std::shared_ptr<const ResidentTerrain>& preview, std::string& error )
    {
        if( m_failedEpoch == m_epoch && m_failedGUID == guid && m_failedRevision == field.GetRevision() ) return false;
        m_requested = true;
        if( m_cancel && m_jobRevision != field.GetRevision() ) m_cancel->store( true, std::memory_order_relaxed );
        return Tick( field, guid, preview, error );
    }


    bool TerrainPreview::Tick( const Heightfield& field, uint64_t guid, std::shared_ptr<const ResidentTerrain>& preview, std::string& error )
    {
        if( m_job.valid() && m_job.wait_for( std::chrono::seconds( 0 ) ) == std::future_status::ready )
        {
            auto result = m_job.get(); m_stats.LastMilliseconds = result.Milliseconds;
            if( m_requested && result.Epoch == m_epoch && result.GUID == guid && result.Revision == field.GetRevision() && !m_cancel->load( std::memory_order_relaxed ) )
            {
                m_requested = false;
                if( result.Data ) { preview = std::move( result.Data ); ++m_stats.Published; error.clear(); }
                else
                {
                    error = result.Error; m_failedEpoch = m_epoch; m_failedGUID = guid; m_failedRevision = field.GetRevision(); return false;
                }
            }
            else ++m_stats.Discarded;
        }
        if( !m_requested ) return error.empty();
        if( preview && preview->AssetGUID == guid && preview->Heights->GetRevision() == field.GetRevision() )
        { m_requested = false; return true; }
        if( m_job.valid() ) return true;
        try
        {
            m_cancel = std::make_shared<std::atomic_bool>( false ); m_jobRevision = field.GetRevision();
            // Capture once only when a worker is available; intermediate edits queue no field copies.
            m_job = std::async( std::launch::async, [snapshot = field, previous = preview, guid, epoch = m_epoch, cancel = m_cancel, build = m_builder]() {
                Result result; result.Epoch = epoch; result.GUID = guid; result.Revision = snapshot.GetRevision();
                const auto started = std::chrono::steady_clock::now();
                try
                {
                    if( !build( snapshot, guid, previous, result.Data, result.Error, cancel.get() ) ) result.Data.reset();
                    if( !result.Data && result.Error.empty() ) result.Error = "Terrain preview preparation failed";
                }
                catch( const std::exception& e ) { result.Data.reset(); result.Error = e.what(); }
                catch( ... ) { result.Data.reset(); result.Error = "Unknown terrain preview failure"; }
                result.Milliseconds = std::chrono::duration<double, std::milli>( std::chrono::steady_clock::now() - started ).count(); return result;
            } );
            ++m_stats.Started; error.clear();
        }
        catch( const std::exception& e )
        { m_requested = false; error = e.what(); m_failedEpoch = m_epoch; m_failedGUID = guid; m_failedRevision = field.GetRevision(); return false; }
        return true;
    }


    bool TerrainPreview::Wait( const Heightfield& field, uint64_t guid, std::shared_ptr<const ResidentTerrain>& preview, std::string& error )
    {
        if( !Request( field, guid, preview, error ) ) return false;
        while( IsBusy() )
        {
            if( m_job.valid() ) m_job.wait();
            if( !Tick( field, guid, preview, error ) ) return false;
        }
        return preview && preview->AssetGUID == guid && preview->Heights->GetRevision() == field.GetRevision();
    }
}
