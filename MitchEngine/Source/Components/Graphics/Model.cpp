#include "PCH.h"
#include "Model.h"
#include "Logger.h"
#include "Renderer.h"
#include "Graphics/FBXModel.h"
#include "Graphics/Shader.h"
#include "Game.h"
#include "Resource/ResourceCache.h"
#include "Graphics/FBXModel.h"

Model::Model(const std::string& path, const std::string& shader)
	: ModelPath(path)
	, ShaderPath(shader)
{
}

Model::~Model()
{
}

void Model::Init()
{
	ModelShader = new Moonlight::Shader((ShaderPath.FullPath + ".vert"), (ShaderPath.FullPath + ".frag"));
	ModelResource = ResourceCache::GetInstance().Get<FBXModel>(ModelPath);
	ModelResource->SetShader(ModelShader);
}

unsigned int Model::GetId()
{
	return Id;
}
