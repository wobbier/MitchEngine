#include "PCH.h"
#include "Model.h"
#include "Logger.h"
#include "Renderer.h"
#include "Graphics/ModelResource.h"
#include "Graphics/ShaderCommand.h"
#include "Game.h"
#include "Resource/ResourceCache.h"
#include "Components/Transform.h"
#include "Mesh.h"
#include "Engine/Engine.h"

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
		ModelShader = new Moonlight::ShaderCommand("Assets/Shaders/SimpleVertexShader.cso", "Assets/Shaders/SimplePixelShader.cso");
		ModelHandle->SetShader(ModelShader);
		auto parentEnt = GetEngine().GetWorld().lock()->GetEntity(Parent);
		if (ModelHandle->RootNode.Nodes.size() > 0)
		{
			for (auto child : ModelHandle->RootNode.Nodes[0].Meshes)
			{
				auto ent = GetEngine().GetWorld().lock()->CreateEntity();
				Transform& trans = ent.lock()->AddComponent<Transform>(child->Name);
				Mesh& ref = ent.lock()->AddComponent<Mesh>(child);
				ref.MeshShader = ModelShader;
				ref.MeshMaterial = child->material;
				trans.SetParent(parentEnt.lock()->GetComponent<Transform>());
			}
		}
	}
}