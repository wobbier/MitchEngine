#pragma once
#include "ICommand.h"
#include "ECS/Entity.h"
#include "Engine/Engine.h"
#include "ECS/Component.h"
#include "ECS/EntityHandle.h"

class CreateEntity
	: public ICommand
{
public:
	CreateEntity()
	{
		Name = "Create Entity";
	}

	virtual void Do() final
	{
		Ent = GetEngine().GetWorld().lock()->CreateEntity();
	}

	virtual void Undo() final
	{
		Ent->MarkForDelete();
	}

	EntityHandle Ent;
};

class AddComponentCommand
	: public ICommand
{
public:
	AddComponentCommand(const std::string& Type, EntityHandle OwningEnt)
	{
		Name = "Add Component: " + Type;
		ComponentName = Type;
		Ent = OwningEnt;
	}

	virtual void Do() override
	{
		Comp = Ent->AddComponentByName(ComponentName);
	}

	virtual void Undo() override
	{
		Ent->RemoveComponent(ComponentName);
		GetEngine().GetWorld().lock()->Simulate();
	}

	BaseComponent* GetComponent() const
	{
		return Comp;
	}

private:
	std::string ComponentName;
	EntityHandle Ent;
	BaseComponent* Comp = nullptr;
};