#pragma once
#include "ECS/Component.h"
#include "ECS/ComponentDetail.h"
#include <string>
#include "Graphics/ShaderCommand.h"
#include "Path.h"
#include "Pointers.h"
#include "Scene/Node.h"
#include "ECS/EntityHandle.h"

class Entity;

class Model
	: public Component<Model>
{
	friend class RenderCore;
public:
	Model();
	Model(const std::string& path);
	~Model() override;

	// Separate init from construction code.
	virtual void Init() final;

	void RecursiveLoadMesh(Moonlight::Node& root, EntityHandle& parentEnt);

	SharedPtr<class ModelResource> ModelHandle = nullptr;
	class Moonlight::ShaderCommand* ModelShader = nullptr;

private:
	Path ModelPath;
	bool IsInitialized = false;

	virtual void OnSerialize(json& outJson) final
	{
		outJson["ModelPath"] = ModelPath.LocalPath;
	}

	virtual void OnDeserialize(const json& inJson) final
	{
		ModelPath = Path(inJson["ModelPath"]);
	}

#if USING( ME_EDITOR )
	virtual void OnEditorInspect() final;
#endif
};

ME_REGISTER_COMPONENT_FOLDER(Model, "Rendering")