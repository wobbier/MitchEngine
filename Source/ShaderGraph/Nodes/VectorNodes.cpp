#include "PCH.h"
#include "ShaderGraph/Nodes/NodeHelpers.h"

namespace ShaderGraph
{
    using namespace Helpers;

    namespace
    {
        // Vector -> its components (missing components read 0).
        class SplitNodeDef
            : public NodeDef
        {
        public:
            SplitNodeDef()
            {
                Type = "Split";
                DisplayName = "Split";
                Category = "Vector/Channel";
                Keywords = "components channels separate rgba xyzw break";
                Inputs = { Dyn( "In" ) };
                Inputs[0].Type = ValueType::Vec4;
                Outputs = { Pin( "R", ValueType::Float ), Pin( "G", ValueType::Float ), Pin( "B", ValueType::Float ), Pin( "A", ValueType::Float ) };
            }

            void Emit( EmitContext& InContext ) const override
            {
                const int width = GetComponentCount( InContext.InType( "In" ) );
                const std::string input = InContext.In( "In" );
                if( width == 1 )
                {
                    InContext.Out( "R", input );
                    InContext.OutAlias( "G", "0.0" );
                    InContext.OutAlias( "B", "0.0" );
                    InContext.OutAlias( "A", "0.0" );
                    return;
                }
                const std::string local = InContext.Local( "In" );
                InContext.Line( std::string( GetShaderTypeName( InContext.InType( "In" ) ) ) + " " + local + " = " + input + ";" );
                static const char* kNames[] = { "R", "G", "B", "A" };
                static const char* kSwizzles[] = { ".x", ".y", ".z", ".w" };
                for( int i = 0; i < 4; ++i )
                {
                    InContext.OutAlias( kNames[i], i < width ? local + kSwizzles[i] : std::string( "0.0" ) );
                }
            }
        };


        // Reorders / picks components: the mask ("xyz", "rg", "wzyx") sets the output width.
        class SwizzleNodeDef
            : public NodeDef
        {
        public:
            SwizzleNodeDef()
            {
                Type = "Swizzle";
                DisplayName = "Swizzle";
                Category = "Vector/Channel";
                Keywords = "reorder components mask channels shuffle";
                Inputs = { Pin( "In", ValueType::Vec4 ) };
                Settings.push_back( MakeSetting( "Mask", SettingKind::String, "xyz", "Components to output, from xyzw or rgba (1-4 of them)." ) );
            }

            static std::string NormalizeMask( const std::string& InMask )
            {
                std::string mask;
                for( const char c : InMask )
                {
                    switch( c )
                    {
                    case 'x': case 'r': mask += 'x'; break;
                    case 'y': case 'g': mask += 'y'; break;
                    case 'z': case 'b': mask += 'z'; break;
                    case 'w': case 'a': mask += 'w'; break;
                    default: break;
                    }
                }
                return mask.substr( 0, 4 );
            }

            void BuildPins( const Graph&, const Node& InNode, std::vector<PinDef>& OutInputs, std::vector<PinDef>& OutOutputs ) const override
            {
                OutInputs = Inputs;
                const std::string mask = NormalizeMask( InNode.Settings.value( "Mask", std::string( "xyz" ) ) );
                OutOutputs = { Pin( "Out", GetVectorType( std::max<int>( 1, static_cast<int>( mask.size() ) ) ) ) };
            }

            std::string GetTitle( const Graph&, const Node& InNode ) const override
            {
                return "Swizzle ." + NormalizeMask( InNode.Settings.value( "Mask", std::string( "xyz" ) ) );
            }

            void Emit( EmitContext& InContext ) const override
            {
                const std::string mask = NormalizeMask( InContext.SettingString( "Mask" ) );
                if( mask.empty() )
                {
                    InContext.Error( "The mask needs 1-4 of x, y, z, w (or r, g, b, a)." );
                    InContext.OutAlias( "Out", "0.0" );
                    return;
                }
                InContext.Out( "Out", "(" + InContext.In( "In" ) + ")." + mask );
            }
        };


        // Moves a position or direction between spaces.
        class TransformNodeDef
            : public NodeDef
        {
        public:
            TransformNodeDef()
            {
                Type = "Transform";
                DisplayName = "Transform";
                Category = "Vector/Space";
                Keywords = "space convert world view tangent object matrix";
                Inputs = { Pin( "In", ValueType::Vec3 ) };
                Outputs = { Pin( "Out", ValueType::Vec3 ) };
                Settings.push_back( EnumSetting( "From", { "World", "View", "Tangent" }, "World" ) );
                Settings.push_back( EnumSetting( "To", { "World", "View", "Tangent" }, "View" ) );
                Settings.push_back( EnumSetting( "Kind", { "Position", "Direction" }, "Direction" ) );
            }

            std::string GetTitle( const Graph&, const Node& InNode ) const override
            {
                return "Transform (" + InNode.Settings.value( "From", std::string( "World" ) ) + " > " + InNode.Settings.value( "To", std::string( "View" ) ) + ")";
            }

            void Emit( EmitContext& InContext ) const override
            {
                const std::string from = InContext.SettingString( "From" );
                const std::string to = InContext.SettingString( "To" );
                const bool position = InContext.SettingString( "Kind" ) == "Position";
                const std::string w = position ? "1.0" : "0.0";
                std::string value = InContext.In( "In" );
                if( from == to )
                {
                    InContext.Out( "Out", value );
                    return;
                }
                // Through world space.
                if( from == "View" )
                {
                    value = "mul(u_invView, vec4(" + value + ", " + w + ")).xyz";
                }
                else if( from == "Tangent" )
                {
                    value = "sgTangentToWorld(" + value + ", " + InContext.BuiltinValue( Builtin::TangentWorld ) + ", " + InContext.BuiltinValue( Builtin::BitangentWorld ) + ", " + InContext.BuiltinValue( Builtin::NormalWorld ) + ")";
                }
                if( to == "View" )
                {
                    value = "mul(u_view, vec4(" + value + ", " + w + ")).xyz";
                }
                else if( to == "Tangent" )
                {
                    value = "sgWorldToTangent(" + value + ", " + InContext.BuiltinValue( Builtin::TangentWorld ) + ", " + InContext.BuiltinValue( Builtin::BitangentWorld ) + ", " + InContext.BuiltinValue( Builtin::NormalWorld ) + ")";
                }
                InContext.Out( "Out", value );
            }
        };
    }


    void RegisterVectorNodes( NodeRegistry& InRegistry )
    {
        InRegistry.Register( std::make_unique<SplitNodeDef>() );
        InRegistry.Register( std::make_unique<SwizzleNodeDef>() );
        InRegistry.Register( std::make_unique<TransformNodeDef>() );

        const char* channel = "Vector/Channel";
        {
            ExpressionNode& node = InRegistry.Expression( "Combine", "Combine", channel );
            node.Keywords = "join append make vector components rgba";
            node.Inputs = { Pin( "R", ValueType::Float ), Pin( "G", ValueType::Float ), Pin( "B", ValueType::Float ), Pin( "A", ValueType::Float, Vector4( 1.f ) ) };
            node.Outputs = { Pin( "RGBA", ValueType::Vec4 ), Pin( "RGB", ValueType::Vec3 ), Pin( "RG", ValueType::Vec2 ) };
            node.Prelude = { "vec4 {L:RGBA} = vec4({R}, {G}, {B}, {A});" };
            node.AliasOutputs = true;
            node.Expressions = { { "RGBA", "{L:RGBA}" }, { "RGB", "{L:RGBA}.xyz" }, { "RG", "{L:RGBA}.xy" } };
        }
        {
            ExpressionNode& node = InRegistry.Expression( "Append", "Append", channel );
            node.Keywords = "combine join vector";
            node.Description = "A Vector 3 and a Float as a Vector 4 (e.g. a colour and its alpha).";
            node.Inputs = { Pin( "RGB", ValueType::Vec3 ), Pin( "A", ValueType::Float, Vector4( 1.f ) ) };
            node.Outputs = { Pin( "Out", ValueType::Vec4 ) };
            node.Expressions = { { "Out", "vec4({RGB}, {A})" } };
        }

        const char* geometry = "Vector/Geometry";
        {
            ExpressionNode& node = InRegistry.Expression( "DotProduct", "Dot Product", geometry );
            node.Keywords = "dot scalar product cosine";
            node.Inputs = { Dyn( "A" ), Dyn( "B" ) };
            node.Inputs[0].Type = node.Inputs[1].Type = ValueType::Vec3;
            node.Outputs = { Pin( "Out", ValueType::Float ) };
            node.Expressions = { { "Out", "dot({A}, {B})" } };
        }
        {
            ExpressionNode& node = InRegistry.Expression( "CrossProduct", "Cross Product", geometry );
            node.Keywords = "cross vector product perpendicular";
            node.Inputs = { Pin( "A", ValueType::Vec3, Vector4( 1.f, 0.f, 0.f, 0.f ) ), Pin( "B", ValueType::Vec3, Vector4( 0.f, 1.f, 0.f, 0.f ) ) };
            node.Outputs = { Pin( "Out", ValueType::Vec3 ) };
            node.Expressions = { { "Out", "cross({A}, {B})" } };
        }
        {
            ExpressionNode& node = InRegistry.Expression( "Normalize", "Normalize", geometry );
            node.Keywords = "unit length direction";
            node.Inputs = { Dyn( "In", 1.f ) };
            node.Inputs[0].Type = ValueType::Vec3;
            node.Outputs = { Dyn( "Out" ) };
            node.Expressions = { { "Out", "normalize({In})" } };
        }
        {
            ExpressionNode& node = InRegistry.Expression( "Length", "Length", geometry );
            node.Keywords = "magnitude size norm";
            node.Inputs = { Dyn( "In" ) };
            node.Inputs[0].Type = ValueType::Vec3;
            node.Outputs = { Pin( "Out", ValueType::Float ) };
            node.Expressions = { { "Out", "length({In})" } };
        }
        {
            ExpressionNode& node = InRegistry.Expression( "Distance", "Distance", geometry );
            node.Keywords = "length between points";
            node.Inputs = { Dyn( "A" ), Dyn( "B" ) };
            node.Inputs[0].Type = node.Inputs[1].Type = ValueType::Vec3;
            node.Outputs = { Pin( "Out", ValueType::Float ) };
            node.Expressions = { { "Out", "distance({A}, {B})" } };
        }
        {
            ExpressionNode& node = InRegistry.Expression( "Reflect", "Reflect", geometry );
            node.Keywords = "mirror bounce";
            node.Inputs = { Pin( "In", ValueType::Vec3 ), Pin( "Normal", ValueType::Vec3, Vector4( 0.f, 1.f, 0.f, 0.f ) ) };
            node.Outputs = { Pin( "Out", ValueType::Vec3 ) };
            node.Expressions = { { "Out", "reflect({In}, {Normal})" } };
        }
        {
            ExpressionNode& node = InRegistry.Expression( "Refract", "Refract", geometry );
            node.Keywords = "bend glass ior eta";
            node.Inputs = { Pin( "In", ValueType::Vec3 ), Pin( "Normal", ValueType::Vec3, Vector4( 0.f, 1.f, 0.f, 0.f ) ), Pin( "Eta", ValueType::Float, Vector4( 0.66f ) ) };
            node.Outputs = { Pin( "Out", ValueType::Vec3 ) };
            node.Expressions = { { "Out", "refract({In}, {Normal}, {Eta})" } };
        }
        {
            ExpressionNode& node = InRegistry.Expression( "Projection", "Projection", geometry );
            node.Keywords = "project onto vector";
            node.Inputs = { Pin( "A", ValueType::Vec3 ), Pin( "B", ValueType::Vec3, Vector4( 0.f, 1.f, 0.f, 0.f ) ) };
            node.Outputs = { Pin( "Out", ValueType::Vec3 ) };
            node.Expressions = { { "Out", "{B} * (dot({A}, {B}) / max(dot({B}, {B}), 1e-8))" } };
        }
        {
            ExpressionNode& node = InRegistry.Expression( "Rejection", "Rejection", geometry );
            node.Keywords = "reject perpendicular component";
            node.Inputs = { Pin( "A", ValueType::Vec3 ), Pin( "B", ValueType::Vec3, Vector4( 0.f, 1.f, 0.f, 0.f ) ) };
            node.Outputs = { Pin( "Out", ValueType::Vec3 ) };
            node.Expressions = { { "Out", "{A} - {B} * (dot({A}, {B}) / max(dot({B}, {B}), 1e-8))" } };
        }
        {
            ExpressionNode& node = InRegistry.Expression( "RotateAboutAxis", "Rotate About Axis", geometry );
            node.Keywords = "rotation spin axis angle";
            node.Inputs = { Pin( "In", ValueType::Vec3 ), Pin( "Axis", ValueType::Vec3, Vector4( 0.f, 1.f, 0.f, 0.f ) ), Pin( "Rotation", ValueType::Float ).Tip( "Radians." ) };
            node.Outputs = { Pin( "Out", ValueType::Vec3 ) };
            node.Expressions = { { "Out", "sgRotateAboutAxis({In}, {Axis}, {Rotation})" } };
        }
    }
}
