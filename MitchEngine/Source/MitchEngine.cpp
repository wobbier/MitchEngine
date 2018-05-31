// 2015 Mitchell Andrews
#include "Engine.h"
#include "Entity.h"
#include "Component.h"
#include <iostream>

int main()
{
	MAN::Engine* engine = new MAN::Engine();

	engine->Init();

	MAN::Entity* e = new MAN::Entity();
	MAN::Component* component = new MAN::Component();
	e->AddComponent(component);
	component = e->GetComponent(0);
	std::cout << component->Handle;

	engine->StartLoop();
	return 0;
}