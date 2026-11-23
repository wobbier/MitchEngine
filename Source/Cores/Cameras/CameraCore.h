#pragma once
#include <algorithm>
#include <string>
#include <vector>
#include "ECS/Core.h"
#include "Camera/CameraData.h"
#include "Components/Camera.h"

class CameraCore
    : public Core<CameraCore>
{
public:
    CameraCore();
    ~CameraCore();

    // Separate init from construction code.
    virtual void Init() final;

    // Each core must update each loop
    virtual void Update( const UpdateContext& inUpdateContext ) final;
    virtual void LateUpdate( const UpdateContext& inUpdateContext ) final;

    virtual void OnEntityAdded( Entity& NewEntity ) final;

    Moonlight::CameraData CreateCameraData( Transform& InTransform, Camera& InCamera );

    void OnEntityRemoved( Entity& InEntity ) override;

    // ME-323: one world is presented a frame (a killcam's replay world, or the game world). A camera
    // core off screen marks its cameras neither main nor rendering, so the renderer presents the other
    // world's main camera; back on screen they render again from the next LateUpdate.
    void SetPresented( bool InPresented ) { m_presented = InPresented; }
    bool IsPresented() const { return m_presented; }

    // Render texture turns (ME-407): cameras with the same UpdateInterval take turns instead of
    // rendering on the same frame. Each gets a slot, its rank among the cameras with its interval
    // ordered by target path and then GUID (so the same every run, ME-369), and renders on the
    // frames where IsRenderTextureTurn holds. Public for the tests.
    struct RenderTextureTurn
    {
        int Interval = 1;
        std::string Target;
        uint64_t Guid = 0;
        Camera* Owner = nullptr;
        int Slot = 0;
    };
    static void AssignRenderTextureTurns( std::vector<RenderTextureTurn>& InOutTurns );
    static bool IsRenderTextureTurn( uint64_t InFrame, int InSlot, int InInterval );
    // ME-406: frames a render texture may go without anything drawing it before its camera skips.
    // Consumers that are render texture cameras themselves only draw on their own turns, so it is
    // the longest interval among them, plus one for UI drawn before the renderer's frame starts.
    static uint32_t UnseenFrameWindow( int InLongestInterval ) { return static_cast<uint32_t>( std::max( InLongestInterval, 1 ) ) + 1u; }

private:
    void OnStart() override;
    void OnStop() override;
    void OnRemovedFromWorld() override;
    void ForgetDefaultCamera();

    bool m_running = false;
    bool m_presented = true;
    bool m_wasHidden = false;
    // Frames this core has scheduled render textures for, and this frame's turns.
    uint64_t m_textureFrame = 0;
    std::vector<RenderTextureTurn> m_turns;
    class Camera* DefaultCamera;
};
