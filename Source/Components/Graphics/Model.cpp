#include "PCH.h"
#include "Model.h"
#include "Logger.h"
#include "Renderer.h"
#include "Graphics/ModelResource.h"
#include "Graphics/Shader.h"
#include "Game.h"
#include "Resource/ResourceCache.h"

Model::Model(const std::string& path)
	: ModelPath(path)
{
}

Model::~Model()
{
}

void Model::Init()
{
	ModelShader = new Moonlight::Shader("Assets/Shaders/SimpleVertexShader.cso", "Assets/Shaders/SimplePixelShader.cso");
	ModelHandle = ResourceCache::GetInstance().Get<ModelResource>(ModelPath);
	ModelHandle->SetShader(ModelShader);
}

unsigned int Model::GetId()
{
	return Id;
}
