#include "Cores/AnimationCore.h"
#include "Components/Sprite.h"
#include "Components/Animation.h"

namespace MAN
{
	AnimationCore::AnimationCore() : Base(ComponentFilter().Requires<Sprite>().Requires<Animation>())
	{
	}

	AnimationCore::~AnimationCore()
	{
	}

	void AnimationCore::Init()
	{
	}

	float i = 0;
	void AnimationCore::Update(float dt)
	{
		auto Animatables = GetEntities();
		for (auto& InEntity : Animatables)
		{
			// Get our components that our Core required
			auto& AnimationComponent = InEntity.GetComponent<Animation>();
			auto& SpriteComponent = InEntity.GetComponent<Sprite>();

			// Update the animations timer for next frame.
			AnimationComponent.FramesAccumulated += dt * AnimationComponent.FPS;
			AnimationComponent.CurrentFrame.x = (int)AnimationComponent.FramesAccumulated;

			if (AnimationComponent.CurrentFrame.x >= AnimationComponent.GridSize.x)
			{
				if (AnimationComponent.GridSize.y)
				{
					if (AnimationComponent.CurrentFrame.y >= AnimationComponent.GridSize.y - 1)
					{
						AnimationComponent.CurrentFrame.y = 0;
					}
					else
					{
						++AnimationComponent.CurrentFrame.y;
					}
					AnimationComponent.FramesAccumulated -= AnimationComponent.CurrentFrame.x;
					AnimationComponent.CurrentFrame.x = 0;
				}
			}
			SpriteComponent.SetSpriteRect(AnimationComponent.CurrentFrame.x * (SpriteComponent.SourceImage->Width / AnimationComponent.GridSize.x),
				AnimationComponent.CurrentFrame.y * (SpriteComponent.SourceImage->Height / AnimationComponent.GridSize.y),
				((SpriteComponent.SourceImage->Width / AnimationComponent.GridSize.x)),
				((SpriteComponent.SourceImage->Height / AnimationComponent.GridSize.y)));
		}
	}
}