#pragma once
#include <ECS/EntityHandle.h>
#include <ECS/ComponentDetail.h>
#include <string>
#include <Components/Transform.h>
#include <JSON.h>

class Transform;

#if USING( ME_EDITOR )

// Drag-and-drop payload for hierarchy entities ("DND_CHILD_TRANSFORM").
struct ParentDescriptor
{
	class Transform* Parent;
};

namespace CommonUtils
{
	// Searchable list of registered components the entity doesn't have yet; adding is undoable.
	// Returns true when a component was added. Pass a filter buffer to keep the search text alive
	// across frames (null uses an internal one).
	bool DrawAddComponentList(const EntityHandle& entity, char* filterBuffer = nullptr, size_t filterBufferSize = 0);

	void SerializeEntity(json& d, Transform* CurrentTransform);
}

#endif
