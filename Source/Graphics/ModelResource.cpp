#include "PCH.h"

#include "ModelResource.h"
#include <assimp/Importer.hpp>
#include <assimp/scene.h>
#include <assimp/postprocess.h>

#include <unordered_map>

#include "CLog.h"
#include "Renderer.h"
#include "Graphics/Texture.h"
#include "Game.h"
#include "Graphics/Material.h"
#include "Resource/ResourceCache.h"
#include "Graphics/MeshData.h"
#include "Scene/Node.h"
#include <stack>
#include "assimp/material.h"
#include "Materials/DiffuseMaterial.h"

ModelResource::ModelResource(const Path& path)
	: Resource(path)
{
	Load();
}

ModelResource::~ModelResource()
{
	Resource::~Resource();

	std::vector<Moonlight::MeshData*> meshes;
	std::stack<Moonlight::Node*> nodes;
	nodes.push(&RootNode);
	while (!nodes.empty())
	{
		Moonlight::Node& child = *nodes.top();
		nodes.pop();
		for (Moonlight::MeshData* childMesh : child.Meshes)
		{
			delete childMesh;
		}
		for (Moonlight::Node& childNode : child.Nodes)
		{
			nodes.push(&childNode);
		}
	}
}

void ModelResource::Load()
{
	Assimp::Importer importer;
	const aiScene* scene = importer.ReadFile(FilePath.FullPath.c_str(), aiProcess_Triangulate | aiProcess_FlipUVs | aiProcess_CalcTangentSpace);
	if (!scene || scene->mFlags & AI_SCENE_FLAGS_INCOMPLETE || !scene->mRootNode)
	{
		std::cout << "ERROR::ASSIMP:: " << importer.GetErrorString() << std::endl;
		return;
	}

	RootNode.Name = std::string(scene->mRootNode->mName.C_Str());
	ProcessNode(scene->mRootNode, scene, RootNode);
	importer.FreeScene();
}

std::vector<Moonlight::MeshData*> ModelResource::GetAllMeshes()
{
	std::vector<Moonlight::MeshData*> meshes;
	std::stack<Moonlight::Node*> nodes;
	nodes.push(&RootNode);
	while (!nodes.empty())
	{
		Moonlight::Node& child = *nodes.top();
		nodes.pop();
		for (Moonlight::MeshData* childMesh : child.Meshes)
		{
			meshes.push_back(childMesh);
		}
		for (Moonlight::Node& childNode : child.Nodes)
		{
			nodes.push(&childNode);
		}
	}
	return meshes;
}

void ModelResource::ProcessNode(aiNode *node, const aiScene *scene, Moonlight::Node& parent)
{
	//parent.Position = Vector3(node->mTransformation[0][0]);
	//parent.Name = node->mName;
	for (unsigned int i = 0; i < node->mNumMeshes; i++)
	{
		aiMesh* mesh = scene->mMeshes[node->mMeshes[i]];
		parent.Meshes.push_back(ProcessMesh(mesh, scene));
	}

	for (unsigned int i = 0; i < node->mNumChildren; i++)
	{
		parent.Nodes.emplace_back();
		parent.Nodes.back().Name = std::string(node->mChildren[i]->mName.C_Str());
		ProcessNode(node->mChildren[i], scene, parent.Nodes.back());
	}
}

Moonlight::MeshData* ModelResource::ProcessMesh(aiMesh *mesh, const aiScene *scene)
{
	std::vector<Moonlight::Vertex> vertices;
	std::vector<uint16_t> indices;
	SharedPtr<Moonlight::Material> newMaterial = std::make_shared<DiffuseMaterial>();
	for (unsigned int i = 0; i < mesh->mNumVertices; i++)
	{
		Moonlight::Vertex vertex;
		DirectX::XMFLOAT3 vector;

		vertex.Position = { mesh->mVertices[i].x,  mesh->mVertices[i].y,  mesh->mVertices[i].z };

		vector.x = mesh->mNormals[i].x;
		vector.y = mesh->mNormals[i].y;
		vector.z = mesh->mNormals[i].z;
		vertex.Normal = vector;

		if (mesh->mTextureCoords[0])
		{
			DirectX::XMFLOAT2 vec;

			// A vertex can contain up to 8 different texture coordinates. We assume that we won't use models where a vertex can have multiple texture coordinates so we always take the first set (0).
			vec.x = mesh->mTextureCoords[0][i].x;
			vec.y = mesh->mTextureCoords[0][i].y;
			vertex.TextureCoord = vec;
		}
		else
		{
			vertex.TextureCoord = DirectX::XMFLOAT2(0.0f, 0.0f);
		}
		if (mesh->mTangents)
		{
			vector.x = mesh->mTangents[i].x;
			vector.y = mesh->mTangents[i].y;
			vector.z = mesh->mTangents[i].z;
			vertex.Tangent = vector;
		}
		if (mesh->mBitangents)
		{
			vector.x = mesh->mBitangents[i].x;
			vector.y = mesh->mBitangents[i].y;
			vector.z = mesh->mBitangents[i].z;
			vertex.BiTangent = vector;
		}
		vertices.push_back(vertex);
	}

	for (unsigned int i = 0; i < mesh->mNumFaces; i++)
	{
		aiFace face = mesh->mFaces[i];

		for (unsigned int j = 0; j < face.mNumIndices; j++)
		{
			indices.push_back(face.mIndices[j]);
		}
	}

	aiMaterial* material = scene->mMaterials[mesh->mMaterialIndex];

	aiColor3D color(0.f, 0.f, 0.f);
	aiColor3D color2(0.f, 0.f, 0.f);
	material->Get(AI_MATKEY_COLOR_TRANSPARENT, color);
	if (color != color2)
	{
		//newMaterial->RenderMode = Moonlight::RenderingMode::Transparent;
	}

	aiColor3D colorDiff(0.f, 0.f, 0.f);
	aiColor3D colorDiff2(0.f, 0.f, 0.f);
	material->Get(AI_MATKEY_COLOR_DIFFUSE, colorDiff);
	if (colorDiff != colorDiff2)
	{
		//newMaterial->RenderMode = Moonlight::RenderingMode::Transparent;
	}

	LoadMaterialTextures(newMaterial, material, aiTextureType_DIFFUSE, Moonlight::TextureType::Diffuse);
	LoadMaterialTextures(newMaterial, material, aiTextureType_SPECULAR, Moonlight::TextureType::Specular);
	LoadMaterialTextures(newMaterial, material, aiTextureType_NORMALS, Moonlight::TextureType::Normal);
	LoadMaterialTextures(newMaterial, material, aiTextureType_HEIGHT, Moonlight::TextureType::Height);
	LoadMaterialTextures(newMaterial, material, aiTextureType_OPACITY, Moonlight::TextureType::Opacity);

	Moonlight::MeshData* output = new Moonlight::MeshData(vertices, indices, newMaterial);
	output->Name = std::string(mesh->mName.C_Str());
	return output;
}

bool ModelResource::LoadMaterialTextures(SharedPtr<Moonlight::Material> newMaterial, aiMaterial *mat, aiTextureType type, const Moonlight::TextureType& typeName)
{
	for (unsigned int i = 0; i < mat->GetTextureCount(type); i++)
	{
		aiString str;
		aiTextureMapping texMapping;
		aiTextureMapMode mapMode;
		mat->GetTexture(type, i, &str, &texMapping, nullptr, nullptr, nullptr, &mapMode);
		std::string stdString = std::string(str.C_Str());
		if (stdString != ".")
		{
			std::string& texturePath = stdString;
			std::shared_ptr<Moonlight::Texture> texture;

			Moonlight::WrapMode wrapMode = Moonlight::WrapMode::Wrap;
			switch (mapMode)
			{
			case aiTextureMapMode_Wrap:
				wrapMode = Moonlight::WrapMode::Wrap;
				break;
			case aiTextureMapMode_Decal:
				wrapMode = Moonlight::WrapMode::Decal;
				break;
			case aiTextureMapMode_Mirror:
				wrapMode = Moonlight::WrapMode::Mirror;
				break;
			case aiTextureMapMode_Clamp:
				wrapMode = Moonlight::WrapMode::Clamp;
				break;
			case _aiTextureMapMode_Force32Bit:
			default:
				wrapMode = Moonlight::WrapMode::Wrap;
				break;
			}

			if (stdString.find(":") != std::string::npos)
			{
				Path filePath(texturePath);
				if (!filePath.Exists)
				{
					return false;
				}
				texture = ResourceCache::GetInstance().Get<Moonlight::Texture>(filePath, wrapMode);
			}
			else
			{
				texture = ResourceCache::GetInstance().Get<Moonlight::Texture>(Path(FilePath.Directory + texturePath), wrapMode);
			}
			texture->Type = typeName;
			newMaterial->SetTexture(typeName, texture);
			return true;
		}
	}
	return false;
}