#pragma once 
#include <glm.hpp>

#include "Graphics/Texture.h"
#include "Graphics/Mesh.h"
#include "Resource/Resource.h"
#include "FilePath.h"
#include "Graphics/Mesh.h"
#include <string>
#include <vector>
#include "Graphics/Material.h"
#include "assimp/material.h"
#include "assimp/mesh.h"
#include "assimp/scene.h"

class ModelResource
	: public Resource
{
	friend class RenderCore;
public:
	ModelResource();
	ModelResource(const FilePath& path);
	~ModelResource();

	static ModelResource* Load(const FilePath& InFilePath);

	void SetShader(Moonlight::Shader* shader);

private:
	void Load();

	std::vector<Moonlight::Mesh*> Meshes;
	std::string Directory;
	std::string Path;
	void ProcessNode(aiNode *node, const aiScene *scene);

	Moonlight::Mesh* ProcessMesh(aiMesh *mesh, const aiScene *scene);

	void LoadMaterialTextures(Moonlight::Material* newMaterial, aiMaterial *mat, aiTextureType type, std::string typeName);

	Moonlight::Shader* ModelShader = nullptr;
};
