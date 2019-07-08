#pragma once
#include "ECS/Core.h"

#include "Engine/Input.h"
#include "Path.h"
#include "Utils/StringUtils.h"

namespace DirectX { class SoundEffectInstance; }
namespace DirectX { class SoundEffect; }
namespace DirectX { class AudioEngine; }

class AudioCore
	: public Core<AudioCore>
{
public:
	AudioCore();

	virtual void Update(float dt) override;


	virtual void OnEntityAdded(Entity& NewEntity) override;

private:
	virtual void Init() override;

	std::unique_ptr<DirectX::AudioEngine> mEngine;
};

ME_REGISTER_CORE(AudioCore);