#pragma once
#include "ECS/Component.h"
#include "ECS/ComponentDetail.h"
#include <string>
#include "Graphics/ShaderCommand.h"
#include "Path.h"
#include "Pointers.h"
#include "Scene/Node.h"
#include "ECS/EntityHandle.h"
#include <memory>
#include <unordered_map>
#include "Math/Bounds.h"

class Entity;
class Mesh;
class Transform;

// A model instance owns private node poses and render parts. Selected nodes can be exposed as
// ordinary attachment entities. Legacy scenes retain their expanded node/Mesh entity hierarchy.
// Loading finishes on the main thread through ExpandPendingModels, before render submission.
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
    // Rebuilds a compact instance from a fresh load of its model, keeping this instance's part and
    // node edits (a regenerated MitchTree, for example).
    void Reload();
    // Evicts the cached model at InFullPath and reloads every instance of it in InWorld (and the
    // terrain foliage that may draw it).
    static void ReloadAll( World& InWorld, const std::string& InFullPath );
    const Path& GetModelPath() const { return ModelPath; }
    // Expands the models whose background load finished (the engine calls this every frame, after
    // finishing loads and before the late update).
    static void ExpandPendingModels();

    // New instances keep import details out of the scene hierarchy. Files without this setting
    // retain the expanded layout, including their entity GUIDs and component edits.
    bool IsCompact() const { return m_compact; }
    struct InstanceNode
    {
        std::string Path;
        const Moonlight::Node* Source = nullptr;
        int ParentIndex = -1;
        std::unique_ptr<Transform> Pose;
    };
    struct RenderPart
    {
        std::string Path;
        int NodeIndex = -1;
        Matrix4 Local;
        std::unique_ptr<Mesh> Renderer;
    };
    const std::vector<InstanceNode>& GetNodes() const { return m_nodes; }
    std::vector<RenderPart>& GetRenderParts() { return m_parts; }
    int FindNode( const std::string& InPathOrName ) const;
    Transform* GetNodeTransform( int InIndex ) const;
    Matrix4 GetNodeWorld( int InIndex ) const;
    EntityHandle ExposeNode( const std::string& InPath );
    bool CompactHierarchy();
    Matrix4 GetPartWorld( const RenderPart& InPart ) const { return GetNodeWorld( InPart.NodeIndex ) * InPart.Local; }
    bool IsPartExternal( const RenderPart& InPart ) const;
    // Capture edits to exposed entities before animation; publish the evaluated poses afterwards.
    static void CaptureExposedEdits( World& InWorld );
    static void UpdateExposedNodes( World& InWorld );
    void UpdatePoseMatrices();
    EntityHandle GetNodeEntity( int InIndex ) const;
    AABB GetWorldBounds( int InNode = -1 ) const;

    void RecursiveLoadMesh( Moonlight::Node& root, EntityHandle& parentEnt, float InPositionScale = 1.f );

    SharedPtr<class ModelResource> ModelHandle = nullptr;
    class Moonlight::ShaderCommand* ModelShader = nullptr;

private:
    Path ModelPath;
    bool IsInitialized = false;
    float m_bakedImportScale = 1.f;
    bool m_hasBakedImportScale = false;
    bool m_compact = true;
    std::vector<InstanceNode> m_nodes;
    std::vector<RenderPart> m_parts;
    std::unordered_map<std::string, int> m_nodeLookup;
    std::unordered_map<int, EntityHandle> m_exposed;
    std::unordered_map<std::string, EntityHandle> m_externalParts;
    json m_savedParts = json::object();
    json m_savedNodes = json::object();
    void Expand();
    void BuildCompact();
    void AddCompactNode( const Moonlight::Node& InNode, int InParent, const std::string& InPath );

    virtual void OnSerialize( json& outJson ) final;

    virtual void OnDeserialize( const json& inJson ) final;

#if USING( ME_EDITOR )
    virtual void OnEditorInspect() final;
#endif
};

ME_REGISTER_COMPONENT_FOLDER( Model, "Rendering" )

// An exposed import node follows its evaluated pose. Its entity name and GUID are independent of
// the source path. Children can carry cameras, scripts, lights or any other ordinary components.
class ModelNode : public Component<ModelNode>
{
    friend class Model;
public:
    ModelNode() : Component( "ModelNode" ) {}
    std::string NodePath;
    std::string PartPath; // existing mesh entity retained during conversion
    Vector3 PositionOffset;
    Quaternion RotationOffset;
    Vector3 ScaleOffset = Vector3( 1.f );
    Model* GetModel() const;
private:
    void CaptureEdit();
    Matrix4 m_lastLocal;
    bool m_written = false;
    void OnSerialize( json& Out ) final;
    void OnDeserialize( const json& In ) final;
#if USING( ME_EDITOR )
    void OnEditorInspect() final;
#endif
};
ME_REGISTER_COMPONENT_FOLDER( ModelNode, "Rendering" )
