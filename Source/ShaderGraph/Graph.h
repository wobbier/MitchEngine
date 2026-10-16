#pragma once
#include <cstdint>
#include <string>
#include <vector>
#include "JSON.h"
#include "Math/Vector2.h"
#include "Math/Vector4.h"
#include "ShaderGraph/ValueType.h"

namespace ShaderGraph
{
    // What the graph renders, which decides its output node, the inputs it can read and where the
    // engine uses the generated shaders.
    enum class Target : uint8_t
    {
        Lit = 0,      // ShaderGraphMaterial: metallic-roughness surface with full engine lighting
        Unlit,        // ShaderGraphMaterial: emitted colour
        Particle,     // ParticleSystem.Graph
        Decal,        // Decal.Graph
        PostProcess,  // PostProcess.Graphs
        Count
    };

    const char* GetTargetName( Target InTarget );
    Target TargetFromName( const std::string& InName, Target InFallback = Target::Lit );

    enum class SurfaceType : uint8_t
    {
        Opaque = 0,
        Transparent
    };

    // Matches Moonlight::BlendMode.
    enum class BlendMode : uint8_t
    {
        Alpha = 0,
        Premultiply,
        Additive,
        Multiply
    };

    // Matches Moonlight::RenderFaceMode: which faces render.
    enum class RenderFace : uint8_t
    {
        Front = 0,
        Back,
        Both
    };

    enum class NormalSpace : uint8_t
    {
        Tangent = 0,
        World
    };

    enum class PositionSpace : uint8_t
    {
        Object = 0,
        World
    };

    // Where a post-process graph runs: on the HDR scene (before bloom, exposure and tonemapping) or
    // on the display image (after tonemapping).
    enum class PostInjection : uint8_t
    {
        BeforeTonemap = 0,
        AfterTonemap
    };

    struct TargetSettings
    {
        Target GraphTarget = Target::Lit;
        SurfaceType Surface = SurfaceType::Opaque;
        BlendMode Blend = BlendMode::Alpha;
        RenderFace Faces = RenderFace::Front;
        bool AlphaClip = false;
        bool CastShadows = true;
        bool Fog = true;
        NormalSpace FragmentNormalSpace = NormalSpace::Tangent;
        PositionSpace OffsetSpace = PositionSpace::Object;
        PostInjection Injection = PostInjection::BeforeTonemap;
        // Particle graphs: shade with the scene's lights (volume lighting) or emit only.
        bool ParticleLit = false;

        void Serialize( json& OutJson ) const;
        void Deserialize( const json& InJson );
    };

    // A blackboard property. Exposed properties appear in the material inspector and can be
    // overridden per material; the rest are graph constants that still live in the parameter block
    // (so the editor can tweak them without recompiling).
    struct Property
    {
        // Stable across renames; Property nodes reference it.
        std::string Id;
        std::string Name;
        ValueType Type = ValueType::Float;
        Vector4 Default = Vector4( 0.f, 0.f, 0.f, 1.f );
        // Texture properties: a project path, or empty for DefaultTexture.
        std::string Texture;
        // "white" / "black" / "gray" / "normal" for unassigned textures.
        std::string DefaultTexture = "white";
        bool IsColor = false;
        bool IsHDR = false;
        bool Exposed = true;
        // Slider range for Float properties (Min == Max: plain drag field).
        float Min = 0.f;
        float Max = 0.f;

        void Serialize( json& OutJson ) const;
        void Deserialize( const json& InJson );
    };

    struct Node
    {
        uint32_t Id = 0;
        // The NodeDef type ("Add", "SampleTexture2D", "Output", ...).
        std::string Type;
        Vector2 Position;
        // Node-specific settings (enum choices, code, the property it reads, ...). Missing keys use
        // the definition's defaults.
        json Settings = json::object();
        // Inline values of unconnected inputs, by pin name.
        std::vector<std::pair<std::string, Vector4>> Defaults;
        bool PreviewExpanded = true;
        // 0 automatic, 1 flat 2D swatch, 2 sphere.
        uint8_t PreviewMode = 0;

        const Vector4* FindDefault( const std::string& InPin ) const;
        void SetDefault( const std::string& InPin, const Vector4& InValue );

        void Serialize( json& OutJson ) const;
        void Deserialize( const json& InJson );
    };

    struct Link
    {
        uint32_t FromNode = 0;
        std::string FromPin;
        uint32_t ToNode = 0;
        std::string ToPin;

        bool operator==( const Link& InOther ) const
        {
            return FromNode == InOther.FromNode && FromPin == InOther.FromPin && ToNode == InOther.ToNode && ToPin == InOther.ToPin;
        }
    };

    // A frame drawn behind nodes (editor only).
    struct Group
    {
        uint32_t Id = 0;
        std::string Title = "Group";
        Vector2 Position;
        Vector2 Size = Vector2( 300.f, 200.f );
        Vector4 Color = Vector4( 0.3f, 0.3f, 0.3f, 0.4f );
    };

    class Graph
    {
    public:
        static constexpr int kVersion = 1;
        static constexpr const char* kExtension = ".shadergraph";

        TargetSettings Settings;
        std::vector<Node> Nodes;
        std::vector<Link> Links;
        std::vector<Property> Properties;
        std::vector<Group> Groups;

        // A new graph for InTarget with just its output node.
        static Graph CreateDefault( Target InTarget );

        Node* FindNode( uint32_t InId );
        const Node* FindNode( uint32_t InId ) const;
        // The single output node (type "Output").
        const Node* FindOutputNode() const;
        Property* FindProperty( const std::string& InId );
        const Property* FindProperty( const std::string& InId ) const;
        // The link feeding an input, or null.
        const Link* FindInputLink( uint32_t InNode, const std::string& InPin ) const;

        Node& AddNode( const std::string& InType, const Vector2& InPosition = Vector2() );
        // Removes the node and its links (the output node can't be removed).
        bool RemoveNode( uint32_t InId );
        // Connects (replacing whatever fed the input). Does not type check (the compiler does).
        void Connect( uint32_t InFromNode, const std::string& InFromPin, uint32_t InToNode, const std::string& InToPin );
        void Disconnect( uint32_t InToNode, const std::string& InToPin );
        Property& AddProperty( const std::string& InName, ValueType InType );
        // Removes the property; nodes reading it stay (and report an error until relinked).
        bool RemoveProperty( const std::string& InId );

        uint32_t AllocateId();
        std::string AllocatePropertyId();

        void Serialize( json& OutJson ) const;
        // False (and Error set) when the JSON isn't a shader graph.
        bool Deserialize( const json& InJson, std::string* OutError = nullptr );

        bool Load( const std::string& InFullPath, std::string* OutError = nullptr );
        bool Save( const std::string& InFullPath ) const;

    private:
        uint32_t m_nextId = 1;
    };

    // "Assets/Foo/Bar.shadergraph" -> "Assets/Foo/Bar.sg": the folder of generated sources.
    std::string GetGeneratedDirectory( const std::string& InGraphPath );
}
