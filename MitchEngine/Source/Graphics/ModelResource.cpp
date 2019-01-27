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
/*
	if (g_pFbxSdkManager == nullptr)
	{
		g_pFbxSdkManager = FbxManager::Create();

		FbxIOSettings* pIOsettings = FbxIOSettings::Create(g_pFbxSdkManager, IOSROOT);
		g_pFbxSdkManager->SetIOSettings(pIOsettings);
	}

	FbxImporter* importer = FbxImporter::Create(g_pFbxSdkManager, "");
	FbxScene* scene = FbxScene::Create(g_pFbxSdkManager, "");

	bool success = importer->Initialize(Path.c_str(), -1, g_pFbxSdkManager->GetIOSettings());

	if (!success)
	{
		Logger::GetInstance().Log(Logger::LogType::Error, "[Model] Failed to find " + Path);
		return;
	}

	success = importer->Import(scene);
	if (!success)
	{
		Logger::GetInstance().Log(Logger::LogType::Error, "[Model] Failed to load scene" + Path);
		return;
	}

	importer->Destroy();
	FbxNode* rootNode = scene->GetRootNode();
	FbxGeometryConverter* converter = new FbxGeometryConverter(g_pFbxSdkManager);
	if (rootNode)
	{
		int vertexCounter = 0;
		for (int i = 0; i < rootNode->GetChildCount(); ++i)
		{
			FbxNode* child = rootNode->GetChild(i);

			if (!child->GetNodeAttribute())
			{
				continue;
			}

			FbxNodeAttribute::EType attributeType = child->GetNodeAttribute()->GetAttributeType();

			if (attributeType != FbxNodeAttribute::eMesh)
			{
				continue;
			}
			converter->Triangulate(child->GetNodeAttribute(), true);
			FbxMesh* mesh = static_cast<FbxMesh*>(child->GetNodeAttribute());
			Meshes.push_back(ProcessMesh(mesh, scene, vertexCounter, child));
			vertexCounter = 0;
		}
	}
	delete converter;*/
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
//
//Moonlight::Mesh* ModelResource::ProcessMesh(FbxMesh* mesh, const FbxScene* scene, int& vertexCounter, FbxNode* child)
//{
//	std::vector<Moonlight::Vertex> vertices;
//	std::vector<unsigned int> indices;
//	std::vector<Moonlight::Material*> materials;
//
//	int triCount = mesh->GetPolygonCount();
//
//	for (int i = 0; i < triCount; ++i)
//	{
//		int numVerts = mesh->GetPolygonSize(i);
//
//		if (numVerts != 3)
//		{
//			Logger::GetInstance().Log(Logger::LogType::Error, "[Mesh] is not triangulated.");
//		}
//
//		for (int j = 0; j < numVerts; ++j)
//		{
//			FbxVector4* verts = mesh->GetControlPoints();
//			int controlPointIndex = mesh->GetPolygonVertex(i, j);
//			FbxVector4 currControlPoint = verts[controlPointIndex];
//
//			Moonlight::Vertex vertex;
//			DirectX::XMFLOAT3 vector;
//
//			vector.x = currControlPoint[0];
//			vector.y = currControlPoint[1];
//			vector.z = currControlPoint[2];
//
//			vertex.Position = vector;
//			vertex.Normal = ReadNormal(mesh, controlPointIndex, vertexCounter);
//			for (int k = 0; k < 1; ++k)
//			{
//				// A vertex can contain up to 8 different texture coordinates. We assume that we won't use models where a vertex can have multiple texture coordinates so we always take the first set (0).
//				//			vec.x = mesh->mTextureCoords[0][i].x;
//				//			vec.y = mesh->mTextureCoords[0][i].y;
//				vertex.TextureCoord = ReadUV(mesh, controlPointIndex, mesh->GetTextureUVIndex(i, j), k);
//			}
//
//
//			vertices.push_back(vertex);
//			indices.push_back(vertexCounter);
//			++vertexCounter;
//		}
//
//	}
//
//	LoadMaterial(child, mesh, materials);
//
//	return new Moonlight::Mesh(vertices, indices, materials);
//}
//
//DirectX::XMFLOAT2 ModelResource::ReadUV(FbxMesh* mesh, int controlPointIndex, int textureUVIndex, int uvLayer)
//{
//	DirectX::XMFLOAT2 uv = DirectX::XMFLOAT2(0, 0);
//	if (uvLayer >= 2 || mesh->GetElementUVCount() <= uvLayer)
//	{
//		return uv;
//	}
//
//	FbxGeometryElementUV* vertexUV = mesh->GetElementUV(uvLayer);
//	FbxLayerElement::EMappingMode mappingMode = vertexUV->GetMappingMode();
//	FbxLayerElement::EReferenceMode refMode = vertexUV->GetReferenceMode();
//
//	switch (mappingMode)
//	{
//	case fbxsdk::FbxLayerElement::eNone:
//		break;
//	case fbxsdk::FbxLayerElement::eByControlPoint:
//		switch (refMode)
//		{
//		case fbxsdk::FbxLayerElement::eDirect:
//			uv.x = (float)vertexUV->GetDirectArray().GetAt(controlPointIndex)[0];
//			uv.y = (float)vertexUV->GetDirectArray().GetAt(controlPointIndex)[1];
//			break;
//		case fbxsdk::FbxLayerElement::eIndex:
//			break;
//		case fbxsdk::FbxLayerElement::eIndexToDirect:
//		{
//			int id = vertexUV->GetIndexArray().GetAt(controlPointIndex);
//			uv.x = (float)vertexUV->GetDirectArray().GetAt(id)[0];
//			uv.y = (float)vertexUV->GetDirectArray().GetAt(id)[1];
//		}
//		break;
//		default:
//			break;
//		}
//		break;
//	case fbxsdk::FbxLayerElement::eByPolygonVertex:
//		switch (refMode)
//		{
//		case fbxsdk::FbxLayerElement::eDirect:
//		case fbxsdk::FbxLayerElement::eIndexToDirect:
//			uv.x = (float)vertexUV->GetDirectArray().GetAt(textureUVIndex)[0];
//			uv.y = (float)vertexUV->GetDirectArray().GetAt(textureUVIndex)[1];
//			break;
//		default:
//			break;
//		}
//		break;
//	case fbxsdk::FbxLayerElement::eByPolygon:
//		break;
//	case fbxsdk::FbxLayerElement::eByEdge:
//		break;
//	case fbxsdk::FbxLayerElement::eAllSame:
//		break;
//	default:
//		break;
//	}
//
//	return uv;
//}
//
//DirectX::XMFLOAT3 ModelResource::ReadNormal(FbxMesh* mesh, int controlPointIndex, int vertextCounter)
//{
//	DirectX::XMFLOAT3 normal;
//
//	if (mesh->GetElementNormalCount() < 1)
//	{
//		throw std::exception("Invalid normals");
//	}
//
//	FbxGeometryElementNormal* vertexNormal = mesh->GetElementNormal(0);
//	switch (vertexNormal->GetMappingMode())
//	{
//	case FbxGeometryElement::eByControlPoint:
//	case FbxGeometryElement::eByPolygonVertex:
//	{
//		switch (vertexNormal->GetReferenceMode())
//		{
//		case FbxGeometryElement::eDirect:
//		{
//			normal.x = static_cast<float>(vertexNormal->GetDirectArray().GetAt(controlPointIndex).mData[0]);
//			normal.y = static_cast<float>(vertexNormal->GetDirectArray().GetAt(controlPointIndex).mData[1]);
//			normal.z = static_cast<float>(vertexNormal->GetDirectArray().GetAt(controlPointIndex).mData[2]);
//		}
//		break;
//		case FbxGeometryElement::eIndexToDirect:
//		{
//			int index = vertexNormal->GetIndexArray().GetAt(controlPointIndex);
//			normal.x = static_cast<float>(vertexNormal->GetDirectArray().GetAt(index).mData[0]);
//			normal.y = static_cast<float>(vertexNormal->GetDirectArray().GetAt(index).mData[1]);
//			normal.z = static_cast<float>(vertexNormal->GetDirectArray().GetAt(index).mData[2]);
//		}
//		break;
//		}
//	}
//	break;
//	default:
//		throw std::exception("Invalid reference");
//		break;
//	}
//	return normal;
//}
//
//void ModelResource::LoadMaterial(FbxNode* node, FbxMesh* mesh, std::vector<Moonlight::Material*>& materials)
//{
//	unsigned int materialCount = node->GetMaterialCount();
//	FbxLayerElementArrayTemplate<int>* arrayThing = new FbxLayerElementArrayTemplate<int>(eFbxInt);
//	mesh->GetMaterialIndices(&arrayThing);
//
//	//mesh->GetLayer(0)->GetMaterials()
//	for (unsigned int i = 0; i < materialCount; ++i)
//	{
//		FbxSurfaceMaterial* surfaceMaterial = node->GetMaterial(i);
//
//		materials.push_back(LoadMaterialTextures(surfaceMaterial));
//	}
//
//}
//
//Moonlight::Material* ModelResource::LoadMaterialTextures(FbxSurfaceMaterial* material)
//{
//	unsigned int textureIndex = 0;
//	FbxProperty property;
//
//	Moonlight::Material* mat = new Moonlight::Material();
//
//	FBXSDK_FOR_EACH_TEXTURE(textureIndex)
//	{
//		property = material->FindProperty(FbxLayerElement::sTextureChannelNames[textureIndex]);
//		if (property.IsValid())
//		{
//			unsigned int textureCount = property.GetSrcObjectCount<FbxTexture>();
//			for (unsigned int i = 0; i < 1; ++i)
//			{
//				FbxLayeredTexture* layeredTexture = property.GetSrcObject<FbxLayeredTexture>(i);
//				if (layeredTexture)
//				{
//					//throw std::exception("Layered texture is unsupported atm");
//				}
//				else
//				{
//					FbxTexture* texture = property.GetSrcObject<FbxTexture>(i);
//					if (texture)
//					{
//						std::string textureType = property.GetNameAsCStr();
//						FbxFileTexture* fileTexture = FbxCast<FbxFileTexture>(texture);
//						if (fileTexture)
//						{
//							std::string filename = fileTexture->GetFileName();
//
//							Moonlight::Texture* loadedTexture = ResourceCache::GetInstance().Get<Moonlight::Texture>(filename);
//							loadedTexture->path = filename;
//							loadedTexture->type = textureType;
//							Logger::GetInstance().Log(Logger::LogType::Info, "Loaded Texture Type: " + loadedTexture->type);
//
//							mat->SetTexture(textureType, loadedTexture);
//						}
//					}
//				}
//			}
//		}
//	}
//
//	return mat;
//}

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
	LoadMaterialTextures(newMaterial, material, aiTextureType_SPECULAR, "texture_specular");
	LoadMaterialTextures(newMaterial, material, aiTextureType_NORMALS, "texture_normal");
	LoadMaterialTextures(newMaterial, material, aiTextureType_HEIGHT, "texture_height");

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
