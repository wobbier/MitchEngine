#include "PCH.h"
#include "Sprite.h"

#include "ECS/Component.h"
#include "ECS/Entity.h"

#include "Components/Transform.h"

Sprite::Sprite()
{
}

Sprite::~Sprite()
{
}

void Sprite::Init()
{
	SetShader("Assets/Shaders/Default.vert", "Assets/Shaders/Default.frag");
}

void Sprite::SetImage(Texture* InTexture)
{
	if (SourceImage != nullptr)
	{
		delete SourceImage;
	}
	SourceImage = InTexture;
	SetSpriteRect(0, 0, SourceImage->Width, SourceImage->Height);
}

void Sprite::SetShader(std::string InVert, std::string InFrag)
{
	CurrentShader = Shader(InVert.c_str(), InFrag.c_str());
}

Texture* Sprite::GetTexture()
{
	return SourceImage;
}

void Sprite::SetSpriteRect(float InX, float InY, float InWidth, float InHeight)
{
	TextureRect.x = InX / SourceImage->Width;
	TextureRect.y = InY / SourceImage->Height;
	TextureRect.z = (InX + InWidth) / SourceImage->Width;
	TextureRect.w = (InY + InHeight) / SourceImage->Height;
	FrameSize = glm::vec2(InWidth, InHeight);
}