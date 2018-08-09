#pragma once
#include "ECS/Component.h"
#include <string>
#include <glm.hpp>

#include <assimp/Importer.hpp>
#include <assimp/scene.h>
#include <assimp/postprocess.h>

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

	void ProcessNode(aiNode *node, const aiScene *scene);

	Mesh ProcessMesh(aiMesh *mesh, const aiScene *scene);

	void LoadMaterialTextures(std::vector<Texture*>& textures, aiMaterial *mat, aiTextureType type, std::string typeName);
};