#include "TerrainTextureSet.h"
#include "Utils/BGFXUtils.h"
#include "JSON.h"
#include <map>
#include <condition_variable>
#include <deque>
#include <functional>
#include <mutex>
#include <thread>

namespace Moonlight
{
    namespace
    {
        class PackingQueue
        {
        public:
            PackingQueue() : Worker( [this] {
                for( ;; )
                {
                    std::function<void()> task;
                    {
                        std::unique_lock lock( Mutex ); Ready.wait( lock, [this] { return Stopping || !Tasks.empty(); } );
                        if( Tasks.empty() ) return;
                        task = std::move( Tasks.front() ); Tasks.pop_front();
                    }
                    task();
                }
            } ) {}
            ~PackingQueue()
            {
                { std::lock_guard lock( Mutex ); Stopping = true; Tasks.clear(); }
                Ready.notify_one(); Worker.join();
            }
            template<class Fn> auto Submit( Fn fn )
            {
                using Result = decltype( fn() );
                auto task = std::make_shared<std::packaged_task<Result()>>( std::move( fn ) );
                auto future = task->get_future();
                { std::lock_guard lock( Mutex ); Tasks.push_back( [task] { ( *task )(); } ); }
                Ready.notify_one(); return future;
            }
            static PackingQueue& Get() { static PackingQueue queue; return queue; }
        private:
            std::mutex Mutex;
            std::condition_variable Ready;
            std::deque<std::function<void()>> Tasks;
            bool Stopping = false;
            std::thread Worker;
        };
    }

    std::shared_ptr<TerrainTextureSet> TerrainTextureSet::Acquire( const TerrainTexturePaths& paths, bool forceAtlas )
    {
        static std::map<std::string, std::weak_ptr<TerrainTextureSet>> cache;
        for( auto it = cache.begin(); it != cache.end(); ) if( it->second.expired() ) it = cache.erase( it ); else ++it;
        const auto* caps = bgfx::getCaps();
        const bool arrays = !forceAtlas && caps->limits.maxTextureLayers >= 4 && caps->limits.maxTextureSize >= 512 &&
            bgfx::isTextureValid( 0, false, 4, bgfx::TextureFormat::RGBA8, BGFX_TEXTURE_NONE );
        const auto layout = arrays ? TerrainTextureLayout::Array : TerrainTextureLayout::Atlas;
        const std::string key = json( paths ).dump() + ( arrays ? ":array" : ":atlas" );
        if( auto found = cache[key].lock() ) return found;
        auto resource = std::shared_ptr<TerrainTextureSet>( new TerrainTextureSet( paths, layout ) );
        cache[key] = resource; return resource;
    }


    TerrainTextureSet::TerrainTextureSet( TerrainTexturePaths paths, TerrainTextureLayout layout ) : m_paths( std::move( paths ) ), m_layout( layout )
    {
        const auto* caps = bgfx::getCaps();
        if( caps->limits.maxTextureSamplers < 16 || caps->limits.maxTextureSize < ( layout == TerrainTextureLayout::Array ? 512u : 1024u ) ||
            !bgfx::isTextureValid( 0, false, layout == TerrainTextureLayout::Array ? 4 : 1, bgfx::TextureFormat::RGBA8, BGFX_TEXTURE_NONE ) )
        { m_supported = false; m_error = "Backend cannot bind the terrain texture pack and lighting"; return; }
        Start();
    }


    TerrainTextureSet::~TerrainTextureSet()
    {
        if( m_cancel ) m_cancel->store( true ); // Promise-backed futures never block a render-frame destructor.
        if( IsGpuAlive() ) for( auto map : m_maps ) if( bgfx::isValid( map ) ) bgfx::destroy( map );
    }


    void TerrainTextureSet::Start()
    {
        m_stamp = TerrainTextureStamp( m_paths ); m_checkAt = std::chrono::steady_clock::now() + std::chrono::seconds( 1 );
        m_cancel = std::make_shared<std::atomic_bool>( false );
        m_work = PackingQueue::Get().Submit( [paths = m_paths, layout = m_layout, cancel = m_cancel] {
            Result result; if( cancel->load() ) return result; result.Success = PackTerrainTextures( paths, layout, result.Data, result.Error ); return result;
        } );
    }


    bool TerrainTextureSet::IsReady() const
    {
        for( auto map : m_maps ) if( !bgfx::isValid( map ) ) return false;
        return true;
    }


    void TerrainTextureSet::Update( bool wait )
    {
        if( !m_supported ) return;
        if( m_work.valid() && ( wait || m_work.wait_for( std::chrono::seconds( 0 ) ) == std::future_status::ready ) )
        {
            auto result = m_work.get();
            if( result.Success ) Upload( std::move( result.Data ) ); else m_error = std::move( result.Error );
        }
        if( !m_work.valid() && std::chrono::steady_clock::now() >= m_checkAt )
        {
            m_checkAt = std::chrono::steady_clock::now() + std::chrono::seconds( 1 );
            if( TerrainTextureStamp( m_paths ) != m_stamp ) Start();
        }
    }


    bool TerrainTextureSet::Upload( TerrainPackedTextures&& data )
    {
        std::array<bgfx::TextureHandle, 3> maps{{ BGFX_INVALID_HANDLE, BGFX_INVALID_HANDLE, BGFX_INVALID_HANDLE }};
        const uint16_t side = data.CellSize * ( m_layout == TerrainTextureLayout::Atlas ? 2 : 1 );
        const uint64_t flags = m_layout == TerrainTextureLayout::Atlas ? BGFX_SAMPLER_U_CLAMP | BGFX_SAMPLER_V_CLAMP : BGFX_SAMPLER_NONE;
        for( unsigned kind = 0; kind < 3; ++kind )
        {
            maps[kind] = bgfx::createTexture2D( side, side, true, m_layout == TerrainTextureLayout::Array ? 4 : 1, bgfx::TextureFormat::RGBA8, flags );
            if( !bgfx::isValid( maps[kind] ) )
            {
                for( auto map : maps ) if( bgfx::isValid( map ) ) bgfx::destroy( map );
                m_error = "Terrain texture pack upload failed; previous textures retained"; return false;
            }
            for( const auto& upload : data.Maps[kind] ) bgfx::updateTexture2D( maps[kind], upload.Layer, upload.Mip, 0, 0, upload.Width, upload.Height,
                bgfx::copy( upload.Pixels.data(), static_cast<uint32_t>( upload.Pixels.size() ) ) );
        }
        for( auto map : m_maps ) if( bgfx::isValid( map ) ) bgfx::destroy( map );
        m_maps = maps; m_error.clear(); return true;
    }
}
