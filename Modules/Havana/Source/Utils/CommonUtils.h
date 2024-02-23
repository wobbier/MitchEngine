#pragma once
#include <ECS/EntityHandle.h>
#include <ECS/ComponentDetail.h>
#include <string>
#include <Commands/EditorCommands.h>
#include <Components/Transform.h>
#include <Utils/CommonUtils.h>
#include <JSON.h>

class Transform;

#if USING( ME_EDITOR )

class FolderTest
{
public:
	std::map<std::string, FolderTest> Folders;
	std::map<std::string, ComponentInfo*> Reg;
};

struct ParentDescriptor
{
	class Transform* Parent;
};

namespace CommonUtils
{
	void RecusiveDelete(EntityHandle ent, Transform* trans);

	void DoComponentRecursive(const FolderTest& currentFolder, const EntityHandle& entity);

	void DrawAddComponentList(const EntityHandle& entity);

	void SerializeEntity(json& d, Transform* CurrentTransform);

	EntityHandle DeserializeEntity(const json& obj, Transform* parent);

	void DuplicateEntity(const EntityHandle& entity);
}

#endif