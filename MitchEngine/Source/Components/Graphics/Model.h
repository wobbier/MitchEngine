#pragma once
#include "ECS/Component.h"
#include <string>
#include <glm.hpp>
#include <fbxsdk.h>

#include "Graphics/Texture.h"
#include "Graphics/Mesh.h"

class Model : public Component<Model>
{
public:
	Shader* ModelShader;
	Model(std::string const &path, const std::string& shader, bool gamma = false);
	~Model();

	void Draw();

	// Separate init from construction code.
	virtual void Init() final;

private:
	bool EnableGammaCorrection;
	std::vector<Mesh> Meshes;
	std::string Directory;

	void Load(std::string const &path);

	static FbxManager* g_pFbxSdkManager;

	Mesh ProcessMesh(FbxMesh* mesh, const FbxScene* scene, int& vertexCounter, FbxNode* child);

	void LoadMaterial(FbxNode* node, FbxMesh* mesh, std::vector<Texture*>& materials);
	void LoadMaterialTextures(FbxSurfaceMaterial* material, std::vector<Texture*>& materials);
	glm::vec2 ReadUV(FbxMesh* mesh, int controlPointIndex, int textureUVIndex, int uvLayer);
	glm::vec3 ReadNormal(FbxMesh* mesh, int controlPointIndex, int vertextCounter);
};