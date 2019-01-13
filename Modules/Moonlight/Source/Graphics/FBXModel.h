#pragma once 
#include <glm.hpp>
#include <fbxsdk.h>

#include "Graphics/Texture.h"
#include "Graphics/Mesh.h"
#include "Resource/Resource.h"

namespace std
{
	template<>
	struct hash<glm::vec3>
	{
		size_t operator()(const glm::vec3& other) const
		{
			return hash<size_t>()(static_cast<size_t>(other.x + other.y + other.z));
		}
	};
}

namespace Moonlight
{
	class FBXModel
		: public Resource
	{
	public:
		FBXModel();
		FBXModel(std::string const &path);
		~FBXModel();

		void Load();

		static FBXModel* Load(const std::string& InFilePath);
		void Draw();

		void SetShader(Shader* shader);
	private:
		std::vector<Mesh> Meshes;
		std::string Directory;
		std::string Path;

		static FbxManager* g_pFbxSdkManager;

		Mesh ProcessMesh(FbxMesh* mesh, const FbxScene* scene, int& vertexCounter, FbxNode* child);

		void LoadMaterial(FbxNode* node, FbxMesh* mesh, std::vector<Texture*>& materials);
		void LoadMaterialTextures(FbxSurfaceMaterial* material, std::vector<Texture*>& materials);
		glm::vec2 ReadUV(FbxMesh* mesh, int controlPointIndex, int textureUVIndex, int uvLayer);
		DirectX::XMFLOAT3 ReadNormal(FbxMesh* mesh, int controlPointIndex, int vertextCounter);
		Shader* ModelShader = nullptr;
	};
}