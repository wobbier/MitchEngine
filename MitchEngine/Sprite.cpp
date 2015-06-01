#include "Sprite.h"
#include "Transform.h"
#include "Component.h"
#include "Entity.h"

using namespace ma;

Sprite::Sprite() {
}

Sprite::~Sprite() {
}

void Sprite::Update(float dt) {

}

void Sprite::Init() {
	SetShader("Shaders/Default.vert", "Shaders/Default.frag");
}

void Sprite::SendMessage(class Message* message) {

}

void ma::Sprite::SetSourceImage(std::string InPath) {
	if (SourceImage != nullptr) {
		delete SourceImage;
	}
	SourceImage = new Texture(InPath);
}

void ma::Sprite::SetShader(std::string InVert, std::string InFrag) {
	CurrentShader = Shader(InVert.c_str(), InFrag.c_str());
}
