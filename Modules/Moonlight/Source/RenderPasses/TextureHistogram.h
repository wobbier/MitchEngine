#pragma once
#include <bgfx/bgfx.h>
#include <glm/glm.hpp>
#include <cstdint>
#include "Pointers.h"

// A 64-bin histogram of any texture, built by two compute dispatches and left on the GPU (no
// readback, no atomics):
//   HistogramBin.comp    a sample grid over the texture, one 16x16 group per tile, each group
//                        bins its 256 samples into a groupshared array and writes its own
//                        64-bin histogram;
//   HistogramMerge.comp  adds the groups' histograms into one.
// The merged histogram (GetMerged) is 16 vec4s of float counts, bin b at element b / 4,
// component b % 4, for a later compute pass to read (a percentile average for eye adaptation,
// ExposureHistogram.comp; a CDF for histogram equalisation of a noise texture or a sky).
//
// To histogram another texture: fill Params (the value to bin is dot( texel, Weights ), through
// log2 when Logarithmic, spread over [Min, Max] in 64 bins; values outside land in the end bins),
// then Dispatch it in two views of your own, ordered after whatever draws the texture and before
// whatever reads the result. The texture is sampled with bilinear taps at the grid points, so a
// small, bright feature can fall between samples: raise the grid size (GroupsX / GroupsY) for
// fine detail. Check IsComputeActive() first and keep a path without it.
namespace Moonlight
{
    struct ProgramRef;

    class TextureHistogram
    {
    public:
        static constexpr uint32_t kBins = 64;
        // Threads (samples) per group along each axis; the grid is groups * kGroupSize wide.
        static constexpr uint32_t kGroupSize = 16;
        // The most groups a histogram can use, which sizes the buffer (it holds kBins / 4 vec4s each).
        static constexpr uint32_t kMaxGroups = 1024;

        struct Params
        {
            glm::vec4 Weights = glm::vec4( 0.2126f, 0.7152f, 0.0722f, 0.f );  // dot with the texel's rgba
            float Min = -14.f;              // the value (log2 of it when Logarithmic) at the first bin's lower edge
            float Max = 6.f;                // and at the last bin's upper edge
            bool Logarithmic = true;
            uint32_t GroupsX = 16;          // sample grid of GroupsX * 16 by GroupsY * 16 over the texture
            uint32_t GroupsY = 9;
        };

        TextureHistogram();
        ~TextureHistogram();
        TextureHistogram( const TextureHistogram& ) = delete;
        TextureHistogram& operator=( const TextureHistogram& ) = delete;

        // Both programs have loaded (a hot reload or a failed compile makes this false for a while).
        bool IsReady() const;

        // Both dispatches, the binning in InBinView and the merge in InMergeView (two views, so
        // the merge sees every group's output). False, and nothing submitted, if not ready.
        bool Dispatch( bgfx::ViewId InBinView, bgfx::ViewId InMergeView, bgfx::TextureHandle InSource, const Params& InParams );

        // The merged histogram: 16 vec4s, readable from a compute pass after Dispatch's views.
        bgfx::DynamicVertexBufferHandle GetMerged() const { return m_merged; }

        // Sets u_histRange (min, max, 1 / (max - min), 0) for a shader that maps bins back to
        // values, as ExposureHistogram.comp does.
        void SetRangeUniform( const Params& InParams ) const;

    private:
        SharedPtr<ProgramRef> m_binProgram;
        SharedPtr<ProgramRef> m_mergeProgram;
        bgfx::DynamicVertexBufferHandle m_groups = BGFX_INVALID_HANDLE;
        bgfx::DynamicVertexBufferHandle m_merged = BGFX_INVALID_HANDLE;
        bgfx::UniformHandle s_histSource = BGFX_INVALID_HANDLE;
        bgfx::UniformHandle u_histGrid = BGFX_INVALID_HANDLE;
        bgfx::UniformHandle u_histWeights = BGFX_INVALID_HANDLE;
        bgfx::UniformHandle u_histRange = BGFX_INVALID_HANDLE;
        bgfx::UniformHandle u_histParams = BGFX_INVALID_HANDLE;
    };
}
