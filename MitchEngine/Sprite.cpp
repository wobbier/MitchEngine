#include "Sprite.h"
#include "Transform.h"
#include "Component.h"
#include "Entity.h"

using namespace ma;

Sprite::Sprite() {
}

Sprite::~Sprite() {
}

void Sprite::Init() {
	SetShader("Shaders/Default.vert", "Shaders/Default.frag");
}

void ma::Sprite::SetImage(Texture* InTexture) {
	if (SourceImage != nullptr) {
		delete SourceImage;
	}
	SourceImage = InTexture;
	SetSpriteRect(0, 0, SourceImage->Width, SourceImage->Height);
}

void ma::Sprite::SetShader(std::string InVert, std::string InFrag) {
	CurrentShader = Shader(InVert.c_str(), InFrag.c_str());
}

void ma::Sprite::SetSpriteRect(float InX, float InY, float InWidth, float InHeight) {
	TextureRect.x = InX / SourceImage->Width;
	TextureRect.y = InY / SourceImage->Height;
	TextureRect.z = (InX + InWidth) / SourceImage->Width;
	TextureRect.w = (InY + InHeight) / SourceImage->Height;
	FrameSize = glm::vec2(InWidth, InHeight);
}
