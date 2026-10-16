#include "PCH.h"
#include "ShaderGraph/Nodes/NodeHelpers.h"

namespace ShaderGraph
{
    using namespace Helpers;

    namespace
    {
        // Sample Texture 2D (and its LOD variant). Vertex-stage samples always use an explicit LOD
        // (there are no derivatives there).
        class SampleTexture2DNodeDef
            : public NodeDef
        {
        public:
            explicit SampleTexture2DNodeDef( bool InExplicitLod )
                : m_explicitLod( InExplicitLod )
            {
                Type = InExplicitLod ? "SampleTexture2DLOD" : "SampleTexture2D";
                DisplayName = InExplicitLod ? "Sample Texture 2D LOD" : "Sample Texture 2D";
                Category = "Texture/Sample";
                Keywords = "texture sample read image tex2d lookup";
                Description = "Reads a texture. Colour textures are converted to linear; normal maps are unpacked to a tangent-space normal.";
                Inputs = { Pin( "Texture", ValueType::Texture2D ), Pin( "UV", ValueType::Vec2 ).Bind( Builtin::UV0 ) };
                if( InExplicitLod )
                {
                    Inputs.push_back( Pin( "LOD", ValueType::Float ) );
                }
                Outputs = { Pin( "RGBA", ValueType::Vec4 ), Pin( "RGB", ValueType::Vec3 ), Pin( "R", ValueType::Float ), Pin( "G", ValueType::Float ), Pin( "B", ValueType::Float ), Pin( "A", ValueType::Float ) };
                Settings.push_back( EnumSetting( "Type", { "Color", "Data", "Normal" }, "Color", "Color: sRGB texture, linearised. Data: raw values (masks, roughness). Normal: a tangent-space normal map." ) );
            }

            std::string GetTitle( const Graph&, const Node& InNode ) const override
            {
                const std::string type = InNode.Settings.value( "Type", std::string( "Color" ) );
                return DisplayName + ( type == "Color" ? "" : " (" + type + ")" );
            }

            void Emit( EmitContext& InContext ) const override
            {
                const std::string texture = InContext.In( "Texture" );
                const std::string uv = InContext.In( "UV" );
                std::string sample;
                if( m_explicitLod || InContext.GetStage() == Stage::Vertex )
                {
                    sample = "texture2DLod(" + texture + ", " + uv + ", " + ( m_explicitLod ? InContext.In( "LOD" ) : std::string( "0.0" ) ) + ")";
                }
                else
                {
                    sample = "texture2D(" + texture + ", " + uv + ")";
                }
                const std::string type = InContext.SettingString( "Type" );
                if( type == "Normal" )
                {
                    const std::string raw = InContext.Local( "Raw" );
                    InContext.Line( "vec4 " + raw + " = " + sample + ";" );
                    sample = "vec4(sgUnpackNormal(" + raw + "), " + raw + ".w)";
                }
                else if( type != "Data" )
                {
                    sample = "toLinear(" + sample + ")";
                }
                InContext.Out( "RGBA", sample );
                const std::string rgba = InContext.Local( "RGBA" );
                InContext.OutAlias( "RGB", rgba + ".xyz" );
                InContext.OutAlias( "R", rgba + ".x" );
                InContext.OutAlias( "G", rgba + ".y" );
                InContext.OutAlias( "B", rgba + ".z" );
                InContext.OutAlias( "A", rgba + ".w" );
            }

        private:
            bool m_explicitLod;
        };
    }


    void RegisterTextureNodes( NodeRegistry& InRegistry )
    {
        InRegistry.Register( std::make_unique<SampleTexture2DNodeDef>( false ) );
        InRegistry.Register( std::make_unique<SampleTexture2DNodeDef>( true ) );

        const char* sample = "Texture/Sample";
        {
            ExpressionNode& node = InRegistry.Expression( "SampleCubemap", "Sample Cubemap", sample );
            node.Keywords = "cube environment reflection skybox texture";
            node.Inputs = { Pin( "Cubemap", ValueType::TextureCube ), Pin( "Direction", ValueType::Vec3 ).Bind( Builtin::NormalWorld ), Pin( "LOD", ValueType::Float ) };
            node.Outputs = { Pin( "Out", ValueType::Vec4 ) };
            node.Expressions = { { "Out", "textureCubeLod({Cubemap}, {Direction}, {LOD})" } };
        }
        {
            ExpressionNode& node = InRegistry.Expression( "Triplanar", "Triplanar", sample );
            node.Keywords = "triplanar projection world box mapping no uv";
            node.Description = "Projects a texture along the three world axes and blends by the normal (no UVs needed).";
            node.Stages = StageFragment;
            node.Inputs = {
                Pin( "Texture", ValueType::Texture2D ),
                Pin( "Position", ValueType::Vec3 ).Bind( Builtin::PositionWorld ),
                Pin( "Normal", ValueType::Vec3 ).Bind( Builtin::NormalWorld ),
                Pin( "Tile", ValueType::Float, Vector4( 1.f ) ),
                Pin( "Blend", ValueType::Float, Vector4( 4.f ) )
            };
            node.Outputs = { Pin( "Out", ValueType::Vec4 ) };
            node.Expressions = { { "Out", "toLinear(sgTriplanar({Texture}, {Position}, {Normal}, {Tile}, {Blend}))" } };
        }
        {
            ExpressionNode& node = InRegistry.Expression( "ParallaxMapping", "Parallax Mapping", "Texture/UV" );
            node.Keywords = "parallax bump offset height depth";
            node.Description = "Offsets UVs by a height map to fake depth.";
            node.Stages = StageFragment;
            node.Inputs = {
                Pin( "Height Map", ValueType::Texture2D ),
                Pin( "Amplitude", ValueType::Float, Vector4( 0.05f ) ),
                Pin( "UV", ValueType::Vec2 ).Bind( Builtin::UV0 )
            };
            node.Outputs = { Pin( "UV", ValueType::Vec2 ) };
            node.Expressions = { { "UV", "sgParallaxOffset({Height Map}, {UV}, {Amplitude}, sgWorldToTangent({B:ViewDirectionWorld}, {B:TangentWorld}, {B:BitangentWorld}, {B:NormalWorld}))" } };
        }

        const char* normal = "Texture/Normal";
        {
            ExpressionNode& node = InRegistry.Expression( "NormalUnpack", "Normal Unpack", normal );
            node.Keywords = "normal map decode tangent";
            node.Inputs = { Pin( "In", ValueType::Vec4, Vector4( 0.5f, 0.5f, 1.f, 1.f ) ) };
            node.Outputs = { Pin( "Out", ValueType::Vec3 ) };
            node.Expressions = { { "Out", "sgUnpackNormal({In})" } };
        }
        {
            ExpressionNode& node = InRegistry.Expression( "NormalStrength", "Normal Strength", normal );
            node.Keywords = "normal intensity flatten bump";
            node.Inputs = { Pin( "In", ValueType::Vec3, Vector4( 0.f, 0.f, 1.f, 0.f ) ), Pin( "Strength", ValueType::Float, Vector4( 1.f ) ) };
            node.Outputs = { Pin( "Out", ValueType::Vec3 ) };
            node.Expressions = { { "Out", "sgNormalStrength({In}, {Strength})" } };
        }
        {
            ExpressionNode& node = InRegistry.Expression( "NormalBlend", "Normal Blend", normal );
            node.Keywords = "normal combine detail layer";
            node.Inputs = { Pin( "A", ValueType::Vec3, Vector4( 0.f, 0.f, 1.f, 0.f ) ), Pin( "B", ValueType::Vec3, Vector4( 0.f, 0.f, 1.f, 0.f ) ) };
            node.Outputs = { Pin( "Out", ValueType::Vec3 ) };
            node.Settings.push_back( EnumSetting( "Mode", { "Whiteout", "Reoriented" }, "Whiteout", "Reoriented: B follows the surface of A (detail maps)." ) );
            node.SwitchSetting = "Mode";
            node.Switch = { { "Whiteout", "sgNormalBlend" }, { "Reoriented", "sgNormalBlendReoriented" } };
            node.Expressions = { { "Out", "{S}({A}, {B})" } };
        }
        {
            ExpressionNode& node = InRegistry.Expression( "NormalFromHeight", "Normal From Height", normal );
            node.Keywords = "bump height derivative normal";
            node.Stages = StageFragment;
            node.Inputs = { Pin( "In", ValueType::Float ), Pin( "Strength", ValueType::Float, Vector4( 0.1f ) ) };
            node.Outputs = { Pin( "Out", ValueType::Vec3 ) };
            node.Expressions = { { "Out", "sgWorldToTangent(sgNormalFromHeight({In}, {Strength}, {B:PositionWorld}, {B:NormalWorld}), {B:TangentWorld}, {B:BitangentWorld}, {B:NormalWorld})" } };
        }
        {
            ExpressionNode& node = InRegistry.Expression( "NormalReconstructZ", "Normal Reconstruct Z", normal );
            node.Keywords = "normal two channel bc5 derive z";
            node.Inputs = { Pin( "In", ValueType::Vec2 ) };
            node.Outputs = { Pin( "Out", ValueType::Vec3 ) };
            node.Expressions = { { "Out", "sgNormalReconstructZ({In})" } };
        }
    }
}
