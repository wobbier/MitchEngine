#pragma once
#include <chrono>
#include <cstdint>
#include <random>

namespace GUID
{
    // Random, non-zero 64-bit identifier (entities, assets).
    inline uint64_t Generate()
    {
        static thread_local std::mt19937_64 generator( std::random_device{}() ^ static_cast<uint64_t>( std::chrono::steady_clock::now().time_since_epoch().count() ) );
        uint64_t guid = 0;
        while( guid == 0 )
        {
            guid = generator();
        }
        return guid;
    }
}
