#include "PCH.h"
#include "Model.h"
#include "CLog.h"
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
	OPTICK_EVENT("Model::Init");
	if (!ModelPath.FullPath.empty())
	{
		ModelHandle = ResourceCache::GetInstance().Get<ModelResource>(ModelPath);
	}
	if (ModelHandle && !IsInitialized)
	{
		IsInitialized = true;
		//ModelShader = 
		
		if (ModelHandle->RootNode.Nodes.size() > 0)
		{
			if (ModelHandle->RootNode.Nodes.size() == 1)
			{
				auto& childNode = ModelHandle->RootNode.Nodes[0];
				{
					//if (childNode.Nodes.size() == 1)
					{
						for (auto& childNodes : childNode.Nodes)
						{
							EntityHandle entityNode = GetEngine().GetWorld().lock()->CreateEntity();
							auto& transform = entityNode->AddComponent<Transform>(childNodes.Name);
							transform.SetParent(Parent->GetComponent<Transform>());
							transform.SetPosition(childNodes.Position);
							RecursiveLoadMesh(childNodes, entityNode);
						}
						for (auto child : childNode.Meshes)
						{
							//Transform& trans = Parent->AddComponent<Transform>(child->Name);
							//Mesh& meshRef =
                            Parent->AddComponent<Mesh>(child);
							//trans.SetPosition(root.Position);
							//meshRef.MeshShader = ModelShader;
							//trans.SetParent(parentEnt->GetComponent<Transform>());
						}
					}
					//else
					{
						//RecursiveLoadMesh(childNode, Parent);
					}
				}
				/*for (auto child : ModelHandle->RootNode.Meshes)
				{
					Transform& trans = Parent->GetComponent<Transform>();
					Mesh& meshRef = Parent->AddComponent<Mesh>(child);
				}*/
			}
			else
			{
				for (auto& childNode : ModelHandle->RootNode.Nodes)
				{
					RecursiveLoadMesh(childNode, Parent);
				}
			}
		}
	}
}

void Model::RecursiveLoadMesh(Moonlight::Node& root, EntityHandle& parentEnt)
{
	for (auto& childNode : root.Nodes)
	{
		EntityHandle entityNode = GetEngine().GetWorld().lock()->CreateEntity();
		auto& transform = entityNode->AddComponent<Transform>(childNode.Name);
		transform.SetParent(parentEnt->GetComponent<Transform>());
		transform.SetPosition(childNode.Position);
		RecursiveLoadMesh(childNode, entityNode);
	}
	for (auto child : root.Meshes)
	{
		auto ent = GetEngine().GetWorld().lock()->CreateEntity();
		Transform& trans = ent->AddComponent<Transform>(child->Name);
        //Mesh& meshRef =
        ent->AddComponent<Mesh>(child);
		trans.SetPosition(root.Position);
		//meshRef.MeshShader = ModelShader;
		trans.SetParent(parentEnt->GetComponent<Transform>());
	}
}
