#include "Core.h"
#include "Engine.h"

using namespace ma;

Core::~Core() {

}

void Core::Init(Engine* e) {
	GameEngine = e;
}
