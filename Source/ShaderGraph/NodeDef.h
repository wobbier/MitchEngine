#pragma once
#include <functional>
#include <map>
#include <memory>
#include <string>
#include <vector>
#include "JSON.h"
#include "Math/Vector4.h"
#include "ShaderGraph/Graph.h"
#include "ShaderGraph/ValueType.h"

namespace ShaderGraph
{
    // Values every target template provides as locals named sg_<Name> (see Targets.cpp for which
    // exist in which target and stage).
    enum class Builtin : uint8_t
    {
        UV0 = 0,
        PositionObject,
        PositionWorld,
        PositionView,
        NormalObject,
        NormalWorld,
        TangentWorld,
        BitangentWorld,
        ViewDirectionWorld,
        CameraPosition,
        ScreenUV,          // 0..1 across the render target
        ScreenPixel,       // gl_FragCoord.xy
        Time,              // seconds since start
        DeltaTime,
        VertexColor,       // particle colour (rgba)
        FrontFacing,
        ParticleRotation,  // radians
        ParticleVelocity,  // world units / second
        DecalFade,         // angle fade 0..1
        ParticleQuadUV,    // the particle quad's own 0..1 UV (UV0 is the flipbook cell's)
        SceneColorInput,   // post-process: the image under this pixel (what the pass replaces)
        Count,
        None = Count
    };

    const char* GetBuiltinName( Builtin InBuiltin );
    ValueType GetBuiltinType( Builtin InBuiltin );

    struct PinDef
    {
        std::string Name;
        ValueType Type = ValueType::Float;
        // Pins sharing a group (>= 0) resolve to the widest vector connected to any of them; the
        // pin's Type is the width used when none is connected.
        int DynamicGroup = -1;
        // The inline value of an unconnected input.
        Vector4 Default;
        // An unconnected input reads this builtin instead of its inline value (UV inputs).
        Builtin DefaultBuiltin = Builtin::None;
        // Output node inputs: the stage that consumes it.
        uint8_t Stages = StageBoth;
        bool IsColor = false;
        // Hidden from the node UI unless connected (rarely used legacy inputs).
        bool Hidden = false;
        std::string Tooltip;

        PinDef() = default;
        PinDef( const std::string& InName, ValueType InType, const Vector4& InDefault = Vector4() )
            : Name( InName ), Type( InType ), Default( InDefault )
        {
        }

        PinDef& Group( int InGroup )
        {
            DynamicGroup = InGroup;
            return *this;
        }
        PinDef& Bind( Builtin InBuiltin )
        {
            DefaultBuiltin = InBuiltin;
            return *this;
        }
        PinDef& Color()
        {
            IsColor = true;
            return *this;
        }
        PinDef& StageOnly( uint8_t InStages )
        {
            Stages = InStages;
            return *this;
        }
        PinDef& Tip( const std::string& InTooltip )
        {
            Tooltip = InTooltip;
            return *this;
        }
    };

    enum class SettingKind : uint8_t
    {
        Bool = 0,
        Int,
        Float,
        Enum,
        String,
        Code,       // multi-line source (Custom Function)
        Texture,    // a project texture path
        Property,   // a blackboard property id
        Vector,     // Vector4 (constants)
        Color,      // Vector4 colour (constants)
        PinList     // Custom Function inputs/outputs: [{"Name","Type"}]
    };

    struct SettingDef
    {
        std::string Name;
        SettingKind Kind = SettingKind::Bool;
        json Default;
        std::vector<std::string> Options;
        std::string Tooltip;
    };

    class Compiler;
    class NodeDef;

    // What a NodeDef::Emit sees: its resolved inputs and a place to declare its outputs. Every
    // expression is in the bgfx shader dialect.
    class EmitContext
    {
    public:
        virtual ~EmitContext() = default;

        virtual Stage GetStage() const = 0;
        virtual Target GetTarget() const = 0;
        virtual const Graph& GetGraph() const = 0;
        virtual const Node& GetNode() const = 0;
        virtual bool IsPreview() const = 0;

        // A setting value (the node's, or the definition's default).
        virtual const json& Setting( const std::string& InName ) const = 0;
        std::string SettingString( const std::string& InName ) const;
        float SettingFloat( const std::string& InName ) const;
        int SettingInt( const std::string& InName ) const;
        bool SettingBool( const std::string& InName ) const;
        Vector4 SettingVector( const std::string& InName ) const;

        // The input as an expression of the pin's resolved type (converted if needed).
        virtual std::string In( const std::string& InPin ) = 0;
        virtual ValueType InType( const std::string& InPin ) const = 0;
        virtual ValueType OutType( const std::string& InPin ) const = 0;
        virtual bool IsConnected( const std::string& InPin ) const = 0;

        // Declares the output as a variable initialised with InExpression.
        virtual void Out( const std::string& InPin, const std::string& InExpression ) = 0;
        // The output *is* InExpression (no variable): for literals, builtins and uniform reads.
        virtual void OutAlias( const std::string& InPin, const std::string& InExpression ) = 0;
        virtual void Line( const std::string& InCode ) = 0;
        // A node-unique local name (n<id>_<InSuffix>).
        virtual std::string Local( const std::string& InSuffix ) const = 0;

        // The builtin's variable (reports an error when the target/stage lacks it).
        virtual std::string BuiltinValue( Builtin InBuiltin ) = 0;
        // A constant. In previews it reads a uniform slot (so editing it needs no recompile);
        // otherwise it's an inline literal. InKey names it within the node ("Value", a pin name).
        virtual std::string Constant( const std::string& InKey, const Vector4& InValue, ValueType InType ) = 0;
        // A property: its parameter-block read or its sampler.
        virtual std::string PropertyValue( const std::string& InPropertyId, ValueType* OutType = nullptr ) = 0;
        // A sampler bound to a project texture (or a default "white"/"normal"/... texture).
        virtual std::string TextureAsset( const std::string& InPath, const std::string& InDefault, ValueType InType ) = 0;
        // Template-provided scene samplers (post-process, decal, particle targets).
        virtual bool HasSceneColor() const = 0;
        virtual bool HasSceneDepth() const = 0;
        // Adds a function declaration once (keyed by InName).
        virtual void Function( const std::string& InName, const std::string& InCode ) = 0;

        virtual void Error( const std::string& InMessage ) = 0;
        virtual void Warning( const std::string& InMessage ) = 0;
    };

    class NodeDef
    {
    public:
        virtual ~NodeDef() = default;

        std::string Type;
        std::string DisplayName;
        // "Math/Basic", "Input/Geometry", ... (the palette's tree).
        std::string Category;
        std::string Keywords;
        std::string Description;
        std::vector<PinDef> Inputs;
        std::vector<PinDef> Outputs;
        std::vector<SettingDef> Settings;
        uint8_t Stages = StageBoth;
        // Bit per Target the node is offered in (all by default).
        uint32_t Targets = ~0u;
        // Not in the palette (the output node).
        bool Hidden = false;
        // Draws a preview by default.
        bool HasPreview = true;

        // The pins of one node instance (Custom Function, Property and Output nodes vary).
        virtual void BuildPins( const Graph& InGraph, const Node& InNode, std::vector<PinDef>& OutInputs, std::vector<PinDef>& OutOutputs ) const;
        virtual uint8_t GetStages( const Graph& InGraph, const Node& InNode ) const;
        // The title shown on the node (Property nodes show the property's name).
        virtual std::string GetTitle( const Graph& InGraph, const Node& InNode ) const;
        virtual void Emit( EmitContext& InContext ) const = 0;

        const SettingDef* FindSetting( const std::string& InName ) const;
        bool SupportsTarget( Target InTarget ) const
        {
            return ( Targets & ( 1u << static_cast<uint32_t>( InTarget ) ) ) != 0;
        }
    };

    // A node defined by expression templates: "{A}" is input A (converted to its resolved type),
    // "{=A}" input A converted to dynamic group 0's type, "{@T}" the shader type of group 0 ("vec3"),
    // "{@T1}" of group 1, "{B:Time}" a builtin, "{L:name}" a node-local name, "{S}" the Switch entry
    // picked by SwitchSetting.
    class ExpressionNode
        : public NodeDef
    {
    public:
        // Output name -> expression template, in output order.
        std::vector<std::pair<std::string, std::string>> Expressions;
        // Lines emitted before the outputs (same substitutions, plus "{L:name}" for a local).
        std::vector<std::string> Prelude;
        // Outputs are the expressions themselves rather than variables (cheap, atomic reads such
        // as builtins or uniform swizzles).
        bool AliasOutputs = false;
        // "{S}" expands to Switch[value of the SwitchSetting setting] (an operator, a function name).
        std::string SwitchSetting;
        std::map<std::string, std::string> Switch;

        void Emit( EmitContext& InContext ) const override;
        static std::string Expand( EmitContext& InContext, const NodeDef& InDef, const std::string& InTemplate );
    };

    class NodeRegistry
    {
    public:
        static NodeRegistry& Get();

        void Register( std::unique_ptr<NodeDef> InDef );
        const NodeDef* Find( const std::string& InType ) const;
        const std::vector<const NodeDef*>& GetAll() const
        {
            return m_ordered;
        }

        // Convenience builder for ExpressionNodes.
        ExpressionNode& Expression( const std::string& InType, const std::string& InDisplayName, const std::string& InCategory );

    private:
        NodeRegistry();
        std::map<std::string, std::unique_ptr<NodeDef>> m_defs;
        std::vector<const NodeDef*> m_ordered;
    };

    // Per-category registration (Nodes/*.cpp), called once by NodeRegistry::Get.
    void RegisterInputNodes( NodeRegistry& InRegistry );
    void RegisterMathNodes( NodeRegistry& InRegistry );
    void RegisterVectorNodes( NodeRegistry& InRegistry );
    void RegisterTextureNodes( NodeRegistry& InRegistry );
    void RegisterUVNodes( NodeRegistry& InRegistry );
    void RegisterProceduralNodes( NodeRegistry& InRegistry );
    void RegisterColorNodes( NodeRegistry& InRegistry );
    void RegisterUtilityNodes( NodeRegistry& InRegistry );
    void RegisterOutputNode( NodeRegistry& InRegistry );

    // A valid shader identifier from a display name ("My Tint 2" -> "MyTint2").
    std::string SanitizeIdentifier( const std::string& InName );
    // FNV-1a: the same on every platform (unlike std::hash), for names in generated code.
    uint32_t StableHash( const std::string& InText );
}
