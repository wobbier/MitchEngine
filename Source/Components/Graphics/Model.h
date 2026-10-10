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

// Expands a model file into child entities named after its nodes, with Mesh components on the
// ones that have geometry (a saved scene's children are reused, keeping their edits). The model
// loads in the background: the children appear once it's ready (ExpandPendingModels, every frame).
class Model
    : public Component<Model>
{
    friend class RenderCore;
public:
    Model();
    Model( const std::string& path );
    ~Model() override;

    // Separate init from construction code.
    virtual void Init() final;

    // The model has loaded and its child entities exist.
    bool IsReady() const
    {
        return IsInitialized;
    }
    // Expands the models whose background load finished (the engine calls this every frame, after
    // finishing loads and before the late update).
    static void ExpandPendingModels();

    void RecursiveLoadMesh( Moonlight::Node& root, EntityHandle& parentEnt );

    SharedPtr<class ModelResource> ModelHandle = nullptr;
    class Moonlight::ShaderCommand* ModelShader = nullptr;

private:
    Path ModelPath;
    bool IsInitialized = false;
    void Expand();

    virtual void OnSerialize( json& outJson ) final
    {
        outJson["ModelPath"] = ModelPath.GetLocalPath();
    }

    virtual void OnDeserialize( const json& inJson ) final
    {
        ModelPath = Path( inJson["ModelPath"] );
    }

#if USING( ME_EDITOR )
    virtual void OnEditorInspect() final;
#endif
};

ME_REGISTER_COMPONENT_FOLDER( Model, "Rendering" )