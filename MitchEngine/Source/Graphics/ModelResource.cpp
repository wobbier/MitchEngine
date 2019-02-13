#include "PCH.h"

#include "ModelResource.h"
#include <assimp/Importer.hpp>
#include <assimp/scene.h>
#include <assimp/postprocess.h>

#include <glm.hpp>

#include <unordered_map>

#include "Logger.h"
#include "Renderer.h"
#include "Graphics/Texture.h"
#include "Game.h"
#include "Graphics/Material.h"
#include "Resource/ResourceCache.h"

ModelResource::ModelResource()
{
}

ModelResource::ModelResource(const FilePath& path)
	: Path(path.FullPath)
	, Directory(path.Directory)
{

}

ModelResource::~ModelResource()
{

}

void ModelResource::Load()
{
	Assimp::Importer importer;
	const aiScene* scene = importer.ReadFile(Path.c_str(), aiProcess_Triangulate | aiProcess_FlipUVs | aiProcess_CalcTangentSpace);
	if (!scene || scene->mFlags & AI_SCENE_FLAGS_INCOMPLETE || !scene->mRootNode)
	{
		std::cout << "ERROR::ASSIMP:: " << importer.GetErrorString() << std::endl;
		return;
	}


	ProcessNode(scene->mRootNode, scene);
}

ModelResource* ModelResource::Load(const FilePath& InFilePath)
{
	ModelResource* model = new ModelResource(InFilePath.FullPath);

	// TODO ASync
	{
		model->Load();
	}

	return model;
}

void ModelResource::SetShader(Moonlight::Shader* shader)
{
	ModelShader = shader;
}

void ModelResource::ProcessNode(aiNode *node, const aiScene *scene)
{
	for (unsigned int i = 0; i < node->mNumMeshes; i++)
	{
		aiMesh* mesh = scene->mMeshes[node->mMeshes[i]];
		Meshes.push_back(ProcessMesh(mesh, scene));
	}

	for (unsigned int i = 0; i < node->mNumChildren; i++)
	{
		ProcessNode(node->mChildren[i], scene);
	}
}

Moonlight::Mesh* ModelResource::ProcessMesh(aiMesh *mesh, const aiScene *scene)
{
	std::vector<Moonlight::Vertex> vertices;
	std::vector<unsigned int> indices;
	Moonlight::Material* newMaterial = new Moonlight::Material();

	for (unsigned int i = 0; i < mesh->mNumVertices; i++)
	{
		Moonlight::Vertex vertex;
		DirectX::XMFLOAT3 vector;

		vector.x = mesh->mVertices[i].x;
		vector.y = mesh->mVertices[i].y;
		vector.z = mesh->mVertices[i].z;
		vertex.Position = vector;

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
			//vertex.Tangent = vector;
		}
		if (mesh->mBitangents)
		{
			vector.x = mesh->mBitangents[i].x;
			vector.y = mesh->mBitangents[i].y;
			vector.z = mesh->mBitangents[i].z;
			//vertex.Bitangent = vector;
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

	LoadMaterialTextures(newMaterial, material, aiTextureType_DIFFUSE, "DiffuseColor");
	LoadMaterialTextures(newMaterial, material, aiTextureType_SPECULAR, "DiffuseSpecular");
	LoadMaterialTextures(newMaterial, material, aiTextureType_NORMALS, "DiffuseNormal");
	LoadMaterialTextures(newMaterial, material, aiTextureType_HEIGHT, "DiffuseHeight");

	return new Moonlight::Mesh(vertices, indices, newMaterial);
}

void ModelResource::LoadMaterialTextures(Moonlight::Material* newMaterial, aiMaterial *mat, aiTextureType type, std::string typeName)
{
	for (unsigned int i = 0; i < mat->GetTextureCount(type); i++)
	{
		aiString str;
		mat->GetTexture(type, i, &str);
		if (std::string(str.C_Str()) != ".")
		{
			Moonlight::Texture* texture = ResourceCache::GetInstance().Get<Moonlight::Texture>(Directory + std::string(str.C_Str()));
			texture->type = typeName;
			texture->path = str.C_Str();
			newMaterial->SetTexture(typeName, texture);
		}
	}
}
