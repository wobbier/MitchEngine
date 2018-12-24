#pragma once
#include "ECS/Component.h"
#include <string>
#include <glm.hpp>
#include <fbxsdk.h>

#include "Graphics/Texture.h"
#include "Graphics/Mesh.h"
#include "Graphics/Common/DeviceResources.h"

class Model : public Component<Model>
{
public:
	Shader* ModelShader;
	Model(std::string const &path, const std::string& shader, bool gamma = false);
	~Model();

	void Draw(std::shared_ptr<DX::DeviceResources> deviceResources);

	// Separate init from construction code.
	virtual void Init() final;

private:
	bool EnableGammaCorrection;
	std::vector<Mesh> Meshes;
	std::string Directory;

	void Load(std::string const &path);

	static FbxManager* g_pFbxSdkManager;

	//void ProcessNode(aiNode *node, const aiScene *scene);

	Mesh ProcessMesh(FbxMesh* mesh, const FbxScene* scene);

	//void LoadMaterialTextures(std::vector<Texture*>& textures, aiMaterial *mat, aiTextureType type, std::string typeName);
};