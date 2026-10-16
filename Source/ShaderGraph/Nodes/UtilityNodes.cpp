#include "PCH.h"
#include "ShaderGraph/Nodes/NodeHelpers.h"

namespace ShaderGraph
{
    using namespace Helpers;

    namespace
    {
        // User shader code with declared inputs and outputs, wrapped in a function.
        class CustomFunctionNodeDef
            : public NodeDef
        {
        public:
            CustomFunctionNodeDef()
            {
                Type = "CustomFunction";
                DisplayName = "Custom Function";
                Category = "Utility";
                Keywords = "code hlsl glsl custom function snippet";
                Description = "Your own shader code (bgfx dialect). Inputs are parameters, outputs are 'out' parameters you assign.";
                Settings.push_back( MakeSetting( "Name", SettingKind::String, "MyFunction" ) );
                Settings.push_back( MakeSetting( "Inputs", SettingKind::PinList, json::array( { { { "Name", "In" }, { "Type", "Vec3" } } } ) ) );
                Settings.push_back( MakeSetting( "Outputs", SettingKind::PinList, json::array( { { { "Name", "Out" }, { "Type", "Vec3" } } } ) ) );
                Settings.push_back( MakeSetting( "Code", SettingKind::Code, "Out = In;" ) );
            }

            static std::vector<PinDef> ReadPins( const json& InList )
            {
                std::vector<PinDef> pins;
                if( !InList.is_array() )
                {
                    return pins;
                }
                for( const json& entry : InList )
                {
                    if( !entry.is_object() )
                    {
                        continue;
                    }
                    PinDef pin( entry.value( "Name", std::string( "In" ) ), ValueTypeFromName( entry.value( "Type", std::string( "Float" ) ) ) );
                    if( pin.Type == ValueType::Mat3 || pin.Type == ValueType::Mat4 )
                    {
                        pin.Type = ValueType::Vec4;
                    }
                    pins.push_back( pin );
                }
                return pins;
            }

            void BuildPins( const Graph&, const Node& InNode, std::vector<PinDef>& OutInputs, std::vector<PinDef>& OutOutputs ) const override
            {
                OutInputs = ReadPins( InNode.Settings.contains( "Inputs" ) ? InNode.Settings["Inputs"] : Settings[1].Default );
                OutOutputs = ReadPins( InNode.Settings.contains( "Outputs" ) ? InNode.Settings["Outputs"] : Settings[2].Default );
            }

            std::string GetTitle( const Graph&, const Node& InNode ) const override
            {
                return InNode.Settings.value( "Name", std::string( "MyFunction" ) );
            }

            void Emit( EmitContext& InContext ) const override
            {
                std::vector<PinDef> inputs;
                std::vector<PinDef> outputs;
                BuildPins( InContext.GetGraph(), InContext.GetNode(), inputs, outputs );
                const std::string name = "sgCustom_" + SanitizeIdentifier( InContext.SettingString( "Name" ) );
                std::string signature = "void " + name + "(";
                std::string call = name + "(";
                bool first = true;
                for( const PinDef& pin : inputs )
                {
                    signature += std::string( first ? "" : ", " ) + GetShaderTypeName( pin.Type ) + " " + SanitizeIdentifier( pin.Name );
                    call += std::string( first ? "" : ", " ) + InContext.In( pin.Name );
                    first = false;
                }
                for( const PinDef& pin : outputs )
                {
                    const std::string local = InContext.Local( pin.Name );
                    InContext.Line( std::string( GetShaderTypeName( pin.Type ) ) + " " + local + ";" );
                    signature += std::string( first ? "" : ", " ) + "out " + GetShaderTypeName( pin.Type ) + " " + SanitizeIdentifier( pin.Name );
                    call += std::string( first ? "" : ", " ) + local;
                    first = false;
                }
                signature += ")";
                call += ");";
                // Functions are keyed by their code, so two Custom Function nodes with the same name
                // but different bodies don't collide (a stable hash: generated files are committed).
                const std::string code = InContext.SettingString( "Code" );
                const std::string functionName = SanitizeIdentifier( name + "_" + std::to_string( StableHash( signature + code ) % 100000u ) );
                std::string declaration = "void " + functionName + signature.substr( 5 + name.size() ) + "\n{\n" + code + "\n}";
                call = functionName + call.substr( name.size() );
                InContext.Function( functionName, declaration );
                InContext.Line( call );
                for( const PinDef& pin : outputs )
                {
                    InContext.OutAlias( pin.Name, InContext.Local( pin.Name ) );
                }
            }
        };
    }


    void RegisterUtilityNodes( NodeRegistry& InRegistry )
    {
        InRegistry.Register( std::make_unique<CustomFunctionNodeDef>() );

        const char* utility = "Utility";
        {
            ExpressionNode& node = InRegistry.Expression( "FresnelEffect", "Fresnel Effect", utility );
            node.Keywords = "fresnel rim edge glow view angle";
            node.Inputs = {
                Pin( "Normal", ValueType::Vec3 ).Bind( Builtin::NormalWorld ),
                Pin( "View Dir", ValueType::Vec3 ).Bind( Builtin::ViewDirectionWorld ),
                Pin( "Power", ValueType::Float, Vector4( 5.f ) )
            };
            node.Outputs = { Pin( "Out", ValueType::Float ) };
            node.Expressions = { { "Out", "pow(1.0 - clamp(dot(sgSafeNormalize({Normal}), sgSafeNormalize({View Dir})), 0.0, 1.0), {Power})" } };
        }
        {
            ExpressionNode& node = InRegistry.Expression( "Preview", "Preview", utility );
            node.Keywords = "preview inspect debug pass through";
            node.Description = "Passes its input through, with a preview (for inspecting a value mid-graph).";
            node.Inputs = { Dyn( "In" ) };
            node.Outputs = { Dyn( "Out" ) };
            node.AliasOutputs = true;
            node.Expressions = { { "Out", "{In}" } };
        }
        {
            ExpressionNode& node = InRegistry.Expression( "Reroute", "Reroute", utility );
            node.Keywords = "redirect wire knot";
            node.HasPreview = false;
            node.Inputs = { Dyn( "In" ) };
            node.Outputs = { Dyn( "Out" ) };
            node.AliasOutputs = true;
            node.Expressions = { { "Out", "{In}" } };
        }

        const char* logic = "Utility/Logic";
        {
            ExpressionNode& node = InRegistry.Expression( "Comparison", "Comparison", logic );
            node.Keywords = "compare equal less greater if test";
            node.HasPreview = false;
            node.Inputs = { Pin( "A", ValueType::Float ), Pin( "B", ValueType::Float ) };
            node.Outputs = { Pin( "Out", ValueType::Bool ) };
            node.Settings.push_back( EnumSetting( "Operator", { "Equal", "NotEqual", "Less", "LessOrEqual", "Greater", "GreaterOrEqual" }, "Less" ) );
            node.SwitchSetting = "Operator";
            node.Switch = { { "Equal", "==" }, { "NotEqual", "!=" }, { "Less", "<" }, { "LessOrEqual", "<=" }, { "Greater", ">" }, { "GreaterOrEqual", ">=" } };
            node.Expressions = { { "Out", "{A} {S} {B}" } };
        }
        {
            ExpressionNode& node = InRegistry.Expression( "Branch", "Branch", logic );
            node.Keywords = "if select choose condition ternary";
            node.Inputs = { Pin( "Predicate", ValueType::Bool ), Dyn( "True", 1.f ), Dyn( "False", 0.f ) };
            node.Outputs = { Dyn( "Out" ) };
            node.Expressions = { { "Out", "{Predicate} ? {True} : {False}" } };
        }
        {
            ExpressionNode& node = InRegistry.Expression( "And", "And", logic );
            node.HasPreview = false;
            node.Inputs = { Pin( "A", ValueType::Bool ), Pin( "B", ValueType::Bool ) };
            node.Outputs = { Pin( "Out", ValueType::Bool ) };
            node.Expressions = { { "Out", "{A} && {B}" } };
        }
        {
            ExpressionNode& node = InRegistry.Expression( "Or", "Or", logic );
            node.HasPreview = false;
            node.Inputs = { Pin( "A", ValueType::Bool ), Pin( "B", ValueType::Bool ) };
            node.Outputs = { Pin( "Out", ValueType::Bool ) };
            node.Expressions = { { "Out", "{A} || {B}" } };
        }
        {
            ExpressionNode& node = InRegistry.Expression( "Not", "Not", logic );
            node.HasPreview = false;
            node.Inputs = { Pin( "In", ValueType::Bool ) };
            node.Outputs = { Pin( "Out", ValueType::Bool ) };
            node.Expressions = { { "Out", "!{In}" } };
        }
    }
}
