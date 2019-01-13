#include "PCH.h"
#include "Model.h"
#include "Logger.h"

#include "Renderer.h"

Model::Model(std::string const &path, const std::string& shader, bool gamma /*= false*/) : EnableGammaCorrection(gamma)
{
	ModelResource = Moonlight::Renderer::GetInstance().GetResources().Get<Moonlight::FBXModel>(path);
	ModelShader = new Moonlight::Shader((shader + ".vert"), (shader + ".frag"));
	ModelResource->SetShader(ModelShader);
	Moonlight::Renderer::GetInstance().PushModel(ModelResource);
}

Model::~Model()
{
}

void Model::Init()
{
}
