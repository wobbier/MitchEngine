#pragma once
#include "ECS/Core.h"
#include "Graphics/Cubemap.h"
#include "Graphics/ShaderCommand.h"
#include "Device/IDevice.h"
#include "Graphics/ModelResource.h"
#include "Jobs/JobSystem.h"

class Mesh;

class RenderCore final
    : public Core<RenderCore>
    , public Moonlight::IDeviceNotify
{
public:
    RenderCore();
    ~RenderCore();

    // Separate init from construction code.
    virtual void Init() final;

    // Each core must update each loop
    virtual void Update( const UpdateContext& inUpdateContext ) final;

    virtual void OnEntityAdded( Entity& NewEntity ) final;
    virtual void OnEntityRemoved( Entity& InEntity ) final;

    // Levels of detail: the relative screen height of each mesh is scaled by this before its level
    // is picked (above 1 keeps detail longer, 0 always draws the coarsest level). --lod-bias sets it.
    float LodBias = 1.f;

    Cubemap* SkyboxMap = nullptr;
    Moonlight::ShaderCommand* SkyboxShader = nullptr;

    virtual void OnDeviceLost() override;
    virtual void OnDeviceRestored() override;

    virtual void OnStart() override;
    virtual void OnStop() override;

#if USING( ME_EDITOR )
    virtual void OnEditorInspect() final;
#endif

private:
    bool EnableDebugDraw = false;
    // The world is playing (editor: levels of detail then follow the game camera).
    bool m_playing = false;
        //Moonlight::Renderer* m_renderer;
};
