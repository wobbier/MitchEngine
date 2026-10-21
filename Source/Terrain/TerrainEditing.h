#pragma once
#include "Terrain/TerrainSurface.h"
#include "Math/Bounds.h"
#include <map>

namespace TerrainData
{
    constexpr uint64_t MaxAuthoringSamples = 1024 * 1024;
    bool ValidateAuthoringSettings( const Settings& InSettings, std::string& OutError );

    enum class BrushTool { Raise, Lower, Smooth, Flatten, Paint, Erase };

    struct BrushSettings
    {
        BrushTool Tool = BrushTool::Raise;
        unsigned Layer = 0;
        float Radius = 4.f;     // Metres.
        float Strength = 0.5f;  // Metres per stamp; blend fraction for Smooth/Flatten.
        float Falloff = 0.65f;  // Fraction of the radius occupied by the soft edge.
        float Spacing = 0.2f;   // Stamp distance as a fraction of the radius.
    };

    struct SampleDelta { SampleCoord Coordinate; float Before = 0.f, After = 0.f; };

    // One distance-spaced gesture. The first stamp is immediate; holding still adds no stamps.
    // All pages must be resident. Shared edges use Heightfield::SetSample's atomic ownership.
    class BrushStroke
    {
    public:
        bool Begin( Heightfield& InField, const BrushSettings& InSettings, double InX, double InZ, std::string& OutError );
        bool Move( Heightfield& InField, double InX, double InZ, std::string& OutError );
        std::vector<SampleDelta> Finish();
        void Cancel( Heightfield& InField );
        bool IsActive() const { return m_active; }

    private:
        bool Stamp( Heightfield& InField, double InX, double InZ, std::string& OutError );
        BrushSettings m_brush;
        std::shared_ptr<const Surface> m_beforeSurface;
        std::unique_ptr<Surface> m_paint;
        std::map<std::pair<int64_t, int64_t>, SampleDelta> m_deltas;
        double m_x = 0, m_z = 0, m_remaining = 0;
        float m_flattenHeight = 0;
        bool m_active = false;
    };

    // Exact source triangle picking, independent of GPU LOD and physics. Terrain-local ray.
    bool RaycastHeightfield( const Heightfield& InField, const Ray& InRay, Vector3& OutPoint, float InMaxDistance = 100000.f );
}
