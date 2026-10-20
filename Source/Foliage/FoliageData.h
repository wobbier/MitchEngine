#pragma once
#include "Terrain/Heightfield.h"
#include "JSON.h"
#include <set>

namespace Foliage
{
    constexpr size_t MaxInstances = 5000, MaxSpecies = 16;
    struct Species
    {
        std::string Name, Model;
        bool operator==( const Species& ) const = default;
    };
    struct Instance
    {
        uint64_t ID = 0;
        uint32_t SpeciesIndex = 0;
        TerrainData::TileCoord Tile;
        Vector3 Position, Normal{ 0, 1, 0 };
        float Scale = 1, Yaw = 0;
        bool AlignNormal = false;
    };
    struct Data
    {
        std::vector<Species> Palette;
        std::vector<Instance> Instances;
        uint64_t NextID = 1;
        bool Validate( std::string& OutError ) const;
    };
    json Encode( const Data& InData );
    bool Decode( const json& InJson, Data& OutData, std::string& OutError );
    struct Brush
    {
        unsigned SpeciesIndex = 0;
        float Radius = 4, Density = 0.08f, MinSpacing = 1;
        float MinScale = 0.8f, MaxScale = 1.2f, MaxSlope = 60, HeightOffset = 0;
        bool AlignNormal = false, Erase = false, EraseAll = false;
        uint32_t Seed = 1234;
        bool operator==( const Brush& ) const = default;
    };
    // Deterministic lattice candidates and distance-spaced stamps, independent of pointer event rate.
    // Positions are terrain-local; deltas own values and never point into a scene or render cache.
    class Stroke
    {
    public:
        bool Begin( const TerrainData::Heightfield& InField, const Data& InData, const Brush& InBrush, double InX, double InZ, std::string& OutError );
        bool Move( const TerrainData::Heightfield& InField, double InX, double InZ, std::string& OutError );
        const Data& GetData() const { return m_data; }
        const std::vector<Instance>& Added() const { return m_added; }
        const std::vector<Instance>& Removed() const { return m_removed; }
        bool Active() const { return m_active; }
        void Stop() { m_active = false; }
    private:
        bool Stamp( const TerrainData::Heightfield& InField, double InX, double InZ, std::string& OutError );
        Data m_data;
        Brush m_brush;
        std::vector<Instance> m_added, m_removed;
        std::set<std::pair<int64_t, int64_t>> m_visited;
        double m_x = 0, m_z = 0, m_remainder = 0;
        uint64_t m_seed = 0;
        bool m_active = false;
    };
}
