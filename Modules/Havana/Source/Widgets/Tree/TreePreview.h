#pragma once
#include "Dementia.h"

#if USING( ME_EDITOR )
#include <cstdint>
#include <string>
#include <vector>
#include <bgfx/bgfx.h>
#include "Math/Vector3.h"
#include "Pointers.h"
#include "imgui.h"

namespace Moonlight
{
    class MeshData;
    class Material;
    struct FrameBuffer;
}
class FoliageMaterial;

// The MitchTree editor's 3D view: the document's tree drawn by the engine's own renderer (so it
// shades exactly as in the game: cascaded shadows, image-based light, translucency, wind) in a
// render group of its own, through an orbit camera with a ground disc. It lights the tree with a
// studio daylight, or borrows the scene camera's sky and post settings (without fog) to show it
// as it will look where it's placed. It only renders while it's drawn.
class TreePreview
{
public:
    TreePreview();
    ~TreePreview();
    TreePreview( const TreePreview& ) = delete;
    TreePreview& operator=( const TreePreview& ) = delete;

    // Takes ownership of a tree's meshes (BuildMeshes), replacing the previous ones. InFrame refits
    // the camera to their bounds.
    void SetMeshes( std::vector<Moonlight::MeshData*> InMeshes, bool InFrame );
    bool HasMeshes() const
    {
        return !m_meshes.empty();
    }

    // The image at the cursor, filling InSize, with orbit (left drag), pan (middle drag or
    // Shift+left), zoom (wheel) and F to frame. Also submits this frame's camera and meshes.
    void Draw( const ImVec2& InSize );
    // Stops rendering until drawn again (the document's window is hidden).
    void Hide();
    void Frame();

    // Writes the next rendered image as a PNG (editor automation). Poll IsCaptureDone.
    void RequestCapture( const std::string& InFullPath );
    bool IsCaptureDone() const
    {
        return m_capturePath.empty();
    }
    const std::string& GetCaptureError() const
    {
        return m_captureError;
    }
    // Frames this preview has rendered (automation waits for a few after a change).
    uint32_t GetRenderedFrames() const
    {
        return m_renderedFrames;
    }
    uint8_t GetRenderGroup() const
    {
        return m_group;
    }
    // The level of detail drawn last frame.
    uint8_t GetDrawnLod() const
    {
        return m_drawnLod;
    }

    // Studio: an even daylight of the preview's own; else the scene camera's sky and look.
    bool SceneLighting = false;
    float SunAzimuth = 35.f;    // degrees (studio light)
    float SunElevation = 50.f;
    bool Wind = true;
    int ForcedLod = -1;         // -1 picks by screen size like the game
    bool Wireframe = false;
    bool ShowGround = true;
    float Yaw = 35.f;           // degrees
    float Pitch = 12.f;
    float Distance = 20.f;
    Vector3 Focus = Vector3( 0.f, 5.f, 0.f );

private:
    struct Part
    {
        Moonlight::MeshData* Mesh = nullptr;
        SharedPtr<FoliageMaterial> Material;    // drawn with (wind switched per frame)
        unsigned int Command = UINT32_MAX;
    };

    void EnsureCamera( uint16_t InWidth, uint16_t InHeight );
    void ReleaseCommands();
    void SubmitCommands();
    void HandleInput( bool InHovered );
    void UpdateCapture();

    uint8_t m_group = 0;
    unsigned int m_camera = UINT32_MAX;
    Moonlight::FrameBuffer* m_buffer = nullptr;
    std::vector<Part> m_meshes;
    unsigned int m_groundCommand = UINT32_MAX;
    SharedPtr<Moonlight::Material> m_groundMaterial;
    uint32_t m_meshGeneration = 0;
    Vector3 m_boundsMin;
    Vector3 m_boundsMax = Vector3( 1.f, 1.f, 1.f );
    uint8_t m_drawnLod = 0;
    uint32_t m_renderedFrames = 0;
    bool m_shown = false;

    std::string m_capturePath;
    std::string m_captureError;
    bgfx::TextureHandle m_captureTexture = BGFX_INVALID_HANDLE;
    std::vector<uint8_t> m_captureData;
    uint32_t m_captureFrame = 0;
    uint16_t m_captureWidth = 0;
    uint16_t m_captureHeight = 0;
};
#endif
