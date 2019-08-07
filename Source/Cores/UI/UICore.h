#pragma once
#include "ECS/Core.h"
#include "Graphics/Cubemap.h"
#include "Graphics/ShaderCommand.h"
#include "Device/IDevice.h"
#include "Renderer.h"
#include "Graphics/ModelResource.h"

class UICore final
	: public Core<UICore>
{
public:
	UICore();
	~UICore();

	// Separate init from construction code.
	virtual void Init() final;

	// Each core must update each loop
	virtual void Update(float dt) final;

	virtual void OnEntityAdded(Entity& NewEntity) final;
	virtual void OnEntityRemoved(Entity& InEntity) final;

	Cubemap* SkyboxMap = nullptr;
	Moonlight::ShaderCommand* SkyboxShader = nullptr;

	virtual void OnStop() override;

private:

	Moonlight::Renderer* m_renderer;
};
