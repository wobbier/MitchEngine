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

Model::Model()
{
}

Model::~Model()
{
}

void Model::Init()
{
	ModelHandle = ResourceCache::GetInstance().Get<ModelResource>(ModelPath);
	if (ModelHandle && !IsInitialized)
	{
		IsInitialized = true;
		ModelShader = new Moonlight::Shader("Assets/Shaders/SimpleVertexShader.cso", "Assets/Shaders/SimplePixelShader.cso");
		ModelHandle->SetShader(ModelShader);
	}
}

unsigned int Model::GetId()
{
	return Id;
}
