#include "PCH.h"
#include "Core.h"
#include <imgui.h>
#include <string>

BaseCore::BaseCore(const char* CompName, const ComponentFilter& Filter)
	: Name(CompName)
	, CompFilter(Filter)
{
	Name = Name.substr(Name.find(' ')+1);
}

World& BaseCore::GetWorld() const
{
	return *GameWorld;
}

const std::vector<Entity>& BaseCore::GetEntities() const
{
	return Entities;
}

std::vector<Entity>& BaseCore::GetEntities()
{
	return Entities;
}

const ComponentFilter& BaseCore::GetComponentFilter() const
{
	return CompFilter;
}

const std::string& BaseCore::GetName() const
{
	return Name;
}

void BaseCore::Add(Entity& InEntity)
{
	Entities.push_back(InEntity);
	OnEntityAdded(InEntity);
}

void BaseCore::Remove(Entity& InEntity)
{
	OnEntityRemoved(InEntity);
	Entities.erase(std::remove(Entities.begin(), Entities.end(), InEntity), Entities.end());
}

void BaseCore::Clear()
{
	Entities.clear();
}

const bool BaseCore::GetIsSerializable() const
{
	return IsSerializable;
}

void BaseCore::SetIsSerializable(bool value)
{
	IsSerializable = value;
}

#if USING( ME_EDITOR )

void BaseCore::OnEditorInspect()
{
	ImGui::Text("Entity Count: %i", Entities.size());
	ImGui::Checkbox("Destroy On Load", &DestroyOnLoad);
}

#endif