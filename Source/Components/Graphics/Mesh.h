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
#include "Graphics/MaterialDetail.h"
#include "Pointers.h"
class MaterialTest
{
public:
	std::map<std::string, MaterialTest> Folders;
	std::map<std::string, MaterialInfo*> Reg;
};
class Mesh
	: public Component<Mesh>
{
	friend class RenderCore;
	friend class EditorCore;
public:
	Mesh();
	Mesh(Moonlight::MeshType InType, Moonlight::Material* InMaterial);
	Mesh(Moonlight::MeshData* mesh);
	~Mesh();

	// Separate init from construction code.
	virtual void Init() final;

	unsigned int GetId();

	Moonlight::MeshData* MeshReferece = nullptr;
	SharedPtr<Moonlight::Material> MeshMaterial;

	Moonlight::MeshType GetType() const;

	virtual void OnSerialize(json& outJson) final;
	virtual void OnDeserialize(const json& inJson) final;
private:
	unsigned int Id = 0;
	Moonlight::MeshType Type;

	std::string GetMeshTypeString(Moonlight::MeshType InType);
	Moonlight::MeshType GetMeshTypeFromString(const std::string& InType);

#if ME_EDITOR
	virtual void OnEditorInspect() final;

	void DoMaterialRecursive(const MaterialTest& currentFolder);
	void SelectMaterial(const std::pair<std::string, MaterialInfo*>& ptr, MaterialRegistry& reg);

#endif
};

ME_REGISTER_COMPONENT_FOLDER(Mesh, "Rendering")