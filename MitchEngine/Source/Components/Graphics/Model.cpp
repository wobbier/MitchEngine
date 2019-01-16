#include "PCH.h"
#include "Model.h"
#include "Logger.h"
#include "Renderer.h"
#include "Graphics/FBXModel.h"
#include "Graphics/Shader.h"
#include "Game.h"

Model::Model(std::string const &path, const std::string& shader, bool gamma /*= false*/) : EnableGammaCorrection(gamma)
{
	ModelResource = Game::GetEngine().GetRenderer().GetResources().Get<Moonlight::FBXModel>(path);
	ModelShader = new Moonlight::Shader((shader + ".vert"), (shader + ".frag"));
	ModelResource->SetShader(ModelShader);
	Game::GetEngine().GetRenderer().PushModel(ModelResource);
}

Model::~Model()
{
}

void Model::Init()
{
}
