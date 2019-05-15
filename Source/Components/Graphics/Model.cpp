#include "PCH.h"
#include "Model.h"
#include "Logger.h"
#include "Renderer.h"
#include "Graphics/ModelResource.h"
#include "Graphics/Shader.h"
#include "Game.h"
#include "Resource/ResourceCache.h"
#include "Components/Transform.h"
#include "Mesh.h"

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
	if (!ModelPath.FullPath.empty())
	{
		ModelHandle = ResourceCache::GetInstance().Get<ModelResource>(ModelPath);
	}
	if (ModelHandle && !IsInitialized)
	{
		IsInitialized = true;
		ModelShader = new Moonlight::Shader("Assets/Shaders/SimpleVertexShader.cso", "Assets/Shaders/SimplePixelShader.cso");
		ModelHandle->SetShader(ModelShader);
		auto parentEnt = Game::GetEngine().GetWorld().lock()->GetEntity(Parent);
		for (auto child : ModelHandle->RootNode.Nodes[0].Meshes)
		{
			auto ent = Game::GetEngine().GetWorld().lock()->CreateEntity();
			Transform& trans = ent.lock()->AddComponent<Transform>(child->Name);
			Mesh& ref = ent.lock()->AddComponent<Mesh>(child);
			ref.MeshShader = ModelShader;
			trans.SetParent(parentEnt->GetComponent<Transform>());
		}
	}
}

unsigned int Model::GetId()
{
	if (!IsInitialized)
	{
		Init();
	}
	return Id;
}
