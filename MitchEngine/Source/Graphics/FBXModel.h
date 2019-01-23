#pragma once 
#include <glm.hpp>
#include <fbxsdk.h>

#include "Graphics/Texture.h"
#include "Graphics/Mesh.h"
#include "Resource/Resource.h"
#include "FilePath.h"
#include <string>
#include <vector>

class FBXModel
	: public Resource
{
	friend class RenderCore;
public:
	FBXModel();
	FBXModel(std::string const &path);
	~FBXModel();

	void Load();

	static FBXModel* Load(const FilePath& InFilePath);
	void Draw();

	void SetShader(Moonlight::Shader* shader);
private:
	std::vector<Moonlight::Mesh*> Meshes;
	std::string Directory;
	std::string Path;

	static FbxManager* g_pFbxSdkManager;

	Moonlight::Mesh* ProcessMesh(FbxMesh* mesh, const FbxScene* scene, int& vertexCounter, FbxNode* child);

	void LoadMaterial(FbxNode* node, FbxMesh* mesh, std::vector<Moonlight::Texture*>& materials);
	void LoadMaterialTextures(FbxSurfaceMaterial* material, std::vector<Moonlight::Texture*>& materials);
	glm::vec2 ReadUV(FbxMesh* mesh, int controlPointIndex, int textureUVIndex, int uvLayer);
	DirectX::XMFLOAT3 ReadNormal(FbxMesh* mesh, int controlPointIndex, int vertextCounter);
	Moonlight::Shader* ModelShader = nullptr;
};
