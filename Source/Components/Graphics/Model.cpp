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
#include "ECS/Entity.h"

Model::Model(const std::string& path)
	: Component("Model")
	, ModelPath(path)
{
}

Model::Model()
	: Component("Model")
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
		ModelShader = new Moonlight::ShaderCommand("Assets/Shaders/DiffuseShader.hlsl");
		
		auto parentEnt = GetEngine().GetWorld().lock()->GetEntity(Parent);
		if (ModelHandle->RootNode.Nodes.size() > 0)
		{
			for (auto& childNode : ModelHandle->RootNode.Nodes)
			{
				RecursiveLoadMesh(childNode, parentEnt);
			}
		}
	}
}

void Model::RecursiveLoadMesh(Moonlight::Node& root, WeakPtr<Entity>& parentEnt)
{
	for (auto& childNode : root.Nodes)
	{
		auto entityNode = GetEngine().GetWorld().lock()->CreateEntity();
		auto& transform = entityNode.lock()->AddComponent<Transform>(childNode.Name);
		transform.SetParent(parentEnt.lock()->GetComponent<Transform>());
		transform.SetPosition(childNode.Position);
		RecursiveLoadMesh(childNode, entityNode);
	}
	for (auto child : root.Meshes)
	{
		auto ent = GetEngine().GetWorld().lock()->CreateEntity();
		Transform& trans = ent.lock()->AddComponent<Transform>(child->Name);
		Mesh& meshRef = ent.lock()->AddComponent<Mesh>(child);
		trans.SetPosition(root.Position);
		meshRef.MeshShader = ModelShader;
		trans.SetParent(parentEnt.lock()->GetComponent<Transform>());
	}
}
