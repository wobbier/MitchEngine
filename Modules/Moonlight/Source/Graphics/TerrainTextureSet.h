#pragma once
#include "Graphics/TerrainTextureCache.h"
#include <bgfx/bgfx.h>
#include <chrono>
#include <atomic>
#include <future>
#include <memory>

namespace Moonlight
{
    // Main-thread adapter. Identical path sets share GPU packs across patches/worlds. One owned
    // process-wide worker packs textures; queued obsolete jobs cancel. Only complete packs publish.
    class TerrainTextureSet
    {
    public:
        static std::shared_ptr<TerrainTextureSet> Acquire( const TerrainTexturePaths& InPaths, bool InForceAtlas = false );
        ~TerrainTextureSet();
        void Update( bool InWait = false );
        bool IsReady() const;
        bool IsLoading() const { return m_work.valid(); }
        const std::string& GetError() const { return m_error; }
        TerrainTextureLayout GetLayout() const { return m_layout; }
        const TerrainTextureCacheInfo& GetCacheInfo() const { return m_cacheInfo; }
        bgfx::TextureHandle GetMap( unsigned InKind ) const { return m_maps[InKind]; }
        uint16_t GetCellSize() const { return 512; }
        uint8_t GetAtlasMaxMip() const { return 7; }
    private:
        TerrainTextureSet( TerrainTexturePaths InPaths, TerrainTextureLayout InLayout );
        void Start();
        bool Upload( TerrainPackedTextures&& InData );
        struct Result { TerrainPackedTextures Data; TerrainTextureCacheInfo Cache; std::string Error; bool Success = false; };
        TerrainTexturePaths m_paths;
        TerrainTextureLayout m_layout;
        bool m_supported = true;
        std::array<bgfx::TextureHandle, 3> m_maps{{ BGFX_INVALID_HANDLE, BGFX_INVALID_HANDLE, BGFX_INVALID_HANDLE }};
        std::future<Result> m_work;
        std::shared_ptr<std::atomic_bool> m_cancel;
        std::string m_stamp, m_error;
        TerrainTextureCacheInfo m_cacheInfo;
        std::chrono::steady_clock::time_point m_checkAt{};
    };
}
