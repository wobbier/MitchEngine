// 2015 Mitchell Andrews
#include "Engine.h"
#include "Entity.h"
#include "Component.h"
#include <iostream>

int main() {
	ma::Engine* engine = new ma::Engine();

	engine->Init();

	ma::Entity* e = new ma::Entity();
	ma::Component* component = new ma::Component();
	e->AddComponent(component);
	component = e->GetComponent(0);

	engine->StartLoop();
	return 0;
}