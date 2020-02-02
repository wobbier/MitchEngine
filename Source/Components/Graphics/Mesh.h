#pragma once
#include "ECS/ComponentDetail.h"
#include "ECS/Component.h"
#include "RenderCommands.h"
#include "Dementia.h"
#include "Graphics/MeshData.h"
#include "Graphics/Texture.h"
#include <string>
#include "Graphics/Material.h"
#include "Graphics/ShaderCommand.h"

class Mesh
	: public Component<Mesh>
{
	friend class RenderCore;
	friend class EditorCore;
public:
	Mesh();
	Mesh(Moonlight::MeshType InType, Moonlight::Material* InMaterial);
	Mesh(Moonlight::MeshData* mesh);

	// Separate init from construction code.
	virtual void Init() final;;

	unsigned int GetId();

	Moonlight::MeshData* MeshReferece = nullptr;
	Moonlight::ShaderCommand* MeshShader = nullptr;
	Moonlight::Material* MeshMaterial = nullptr;

	Moonlight::MeshType GetType() const;

	virtual void Serialize(json& outJson) final;
	virtual void Deserialize(const json& inJson) final;
private:
	unsigned int Id = 0;
	Moonlight::MeshType Type;

	std::string GetMeshTypeString(Moonlight::MeshType InType);
	Moonlight::MeshType GetMeshTypeFromString(const std::string& InType);

#if ME_EDITOR
	virtual void OnEditorInspect() final;
#endif
};

ME_REGISTER_COMPONENT(Mesh)