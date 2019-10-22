#pragma once
#include "ICommand.h"
#include "ECS/Entity.h"
#include "Pointers.h"
#include "Engine/Engine.h"
#include "ECS/Component.h"

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
		Ent.lock()->MarkForDelete();
	}

	WeakPtr<Entity> Ent;
};

class AddComponentCommand
	: public ICommand
{
public:
	AddComponentCommand(const std::string& Type, WeakPtr<Entity> OwningEnt)
	{
		Name = "Add Component: " + Type;
		ComponentName = Type;
		Ent = OwningEnt;
	}

	virtual void Do() override
	{
		Comp = Ent.lock()->AddComponentByName(ComponentName);
	}

	virtual void Undo() override
	{
		Ent.lock()->RemoveComponent(ComponentName);
		GetEngine().GetWorld().lock()->Simulate();
	}

	BaseComponent* GetComponent() const
	{
		return Comp;
	}

private:
	std::string ComponentName;
	WeakPtr<Entity> Ent;
	BaseComponent* Comp = nullptr;
};