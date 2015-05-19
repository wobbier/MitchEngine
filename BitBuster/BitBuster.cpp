// 2015 Mitchell Andrews
#include "Engine.h"
#include "Entity.h"
#include "Component.h"
#include <iostream>
using namespace ma;
int main() {
	Engine* engine = new Engine();

	engine->Init();

	Entity* e = new Entity();
	Component* component = new Component();
	e->AddComponent(component);
	component = e->GetComponent(0);

	engine->StartLoop();
	return 0;
}