#include "AnimationCore.h"
#include "Sprite.h"
#include "Animation.h"

namespace ma {
	AnimationCore::AnimationCore() : Base(ComponentFilter().Requires<Sprite>().Requires<Animation>()) {
	}


	AnimationCore::~AnimationCore() {
	}

	void AnimationCore::Init() {

	}
	float i = 0;
	void AnimationCore::Update(float dt) {
		auto Renderables = GetEntities();
		for (auto& InEntity : Renderables) {
			auto& AnimationComponent = InEntity.GetComponent<Animation>();
			auto& SpriteComponent = InEntity.GetComponent<Sprite>();

			SpriteComponent.SetSpriteRect(0, 0, i += dt, SpriteComponent.SourceImage->Height);
		}
	}
}