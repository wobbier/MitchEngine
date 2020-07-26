#include "PCH.h"
#include "SelfDestructCore.h"

void SelfDestructor::Update(float dt)
{
	auto entities = GetEntities();
	for (Entity& entity : entities)
	{
		SelfDestruct& destruct = entity.GetComponent<SelfDestruct>();
		destruct.Lifetime -= dt;
		if (destruct.Lifetime <= 0)
		{
			entity.MarkForDelete();
		}
	}
}
