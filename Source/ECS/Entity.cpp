#include "PCH.h"
#include "Entity.h"
#include "Engine/World.h"
#include "ComponentDetail.h"
#include "Logger.h"

#if ME_EDITOR
#include "imgui.h"
#endif

Entity::Entity()
{
}

Entity::Entity(World& inWorld, EntityID inId) :
	Id(inId),
	GameWorld(&inWorld)
{
}

Entity::~Entity()
{
}

const bool Entity::HasComponent(TypeId inComponentType) const
{
	ComponentTypeArray ar = GameWorld->EntityAttributes.Storage.GetComponentTypes(*this);

	return ar[inComponentType] == true;
}

void Entity::AddComponent(BaseComponent* inComponent, TypeId inComponentTypeId)
{
	inComponent->Parent = EntityHandle(GetId(), GameWorld->GetSharedPtr());
	GameWorld->EntityAttributes.Storage.AddComponent(*this, inComponent, inComponentTypeId);
	if(!IsLoading)
	SetActive(true);
}

BaseComponent* Entity::AddComponentByName(const std::string& inComponent)
{
	ComponentRegistry& reg = GetComponentRegistry();
	ComponentRegistry::iterator it = reg.find(inComponent);

	if (it == reg.end()) {
		Logger::GetInstance().Log(Logger::LogType::Warning, "Factory not found for component " + inComponent);
		return nullptr;
	}

	return it->second.CreateFunc(*this);
}

const EntityID& Entity::GetId() const
{
	return Id;
}

BaseComponent& Entity::GetComponent(TypeId InTypeId) const
{
	return GameWorld->EntityAttributes.Storage.GetComponent(*this, InTypeId);
}

void Entity::SetActive(const bool InActive)
{
	GameWorld->ActivateEntity(*this, InActive);
}

void Entity::MarkForDelete()
{
	GameWorld->MarkEntityForDelete(*this);
	GameWorld = nullptr;
}

#if ME_EDITOR

void Entity::OnEditorInspect()
{
	ImGui::Checkbox("Destroy On Load", &DestroyOnLoad);
}

#endif

bool Entity::operator==(const Entity& entity) const
{
	return Id == entity.Id && entity.GameWorld == GameWorld;
}

void Entity::RemoveComponent(TypeId InComponentTypeId)
{
	GameWorld->EntityAttributes.Storage.RemoveComponent(*this, InComponentTypeId);
}

void Entity::RemoveComponent(const std::string& Name)
{
	ComponentRegistry& reg = GetComponentRegistry();
	ComponentRegistry::iterator it = reg.find(Name);

	if (it == reg.end())
	{
		BRUH("Factory not found for component " + Name);
	}

	RemoveComponent(it->second.GetTypeFunc());
}

std::vector<BaseComponent*> Entity::GetAllComponents() const
{
	return GameWorld->EntityAttributes.Storage.GetAllComponents(*this);
}
