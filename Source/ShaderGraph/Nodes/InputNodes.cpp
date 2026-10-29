#include "PCH.h"
#include "ShaderGraph/Nodes/NodeHelpers.h"

namespace ShaderGraph
{
    using namespace Helpers;

    namespace
    {
        // Float / Vector 2-4 constants: the value is a setting (in previews it's a uniform, so
        // dragging it doesn't recompile).
        class ConstantNodeDef
            : public NodeDef
        {
        public:
            ConstantNodeDef( const char* InType, const char* InName, ValueType InValueType, bool InIsColor )
                : m_type( InValueType )
                , m_isColor( InIsColor )
            {
                Type = InType;
                DisplayName = InName;
                Category = "Input/Constants";
                Keywords = InIsColor ? "colour rgb rgba constant" : "constant value number vector";
                Outputs.push_back( Pin( "Out", InValueType ) );
                Settings.push_back( MakeSetting( "Value", InIsColor ? SettingKind::Color : SettingKind::Vector, InIsColor ? json::array( { 1.f, 1.f, 1.f, 1.f } ) : json::array( { 0.f, 0.f, 0.f, 0.f } ) ) );
                if( InIsColor )
                {
                    Settings.push_back( MakeSetting( "HDR", SettingKind::Bool, false, "Allow values above 1 (emission)." ) );
                    Description = "A colour (picked in sRGB, output linear).";
                }
            }

            void Emit( EmitContext& InContext ) const override
            {
                Vector4 value = InContext.SettingVector( "Value" );
                if( m_isColor )
                {
                    // Picked in sRGB (like every colour in the editor), shaded in linear.
                    value = Vector4( SrgbToLinear( value.x ), SrgbToLinear( value.y ), SrgbToLinear( value.z ), value.w );
                }
                InContext.OutAlias( "Out", InContext.Constant( "Value", value, m_type ) );
            }

        private:
            ValueType m_type;
            bool m_isColor = false;
        };


        class BooleanNodeDef
            : public NodeDef
        {
        public:
            BooleanNodeDef()
            {
                Type = "Boolean";
                DisplayName = "Boolean";
                Category = "Input/Constants";
                Keywords = "bool true false toggle";
                Outputs.push_back( Pin( "Out", ValueType::Bool ) );
                Settings.push_back( MakeSetting( "Value", SettingKind::Bool, false ) );
            }

            void Emit( EmitContext& InContext ) const override
            {
                InContext.OutAlias( "Out", InContext.SettingBool( "Value" ) ? "true" : "false" );
            }
        };


        // Reads a blackboard property (its parameter-block value or its texture).
        class PropertyNodeDef
            : public NodeDef
        {
        public:
            PropertyNodeDef()
            {
                Type = "Property";
                DisplayName = "Property";
                Category = "Input/Properties";
                Keywords = "parameter blackboard exposed uniform";
                Description = "A blackboard property. Exposed properties can be set per material.";
                Hidden = true; // created by dragging from the blackboard
                Settings.push_back( MakeSetting( "Property", SettingKind::Property, "" ) );
            }

            void BuildPins( const Graph& InGraph, const Node& InNode, std::vector<PinDef>& OutInputs, std::vector<PinDef>& OutOutputs ) const override
            {
                OutInputs.clear();
                OutOutputs.clear();
                const std::string id = InNode.Settings.value( "Property", std::string() );
                const Property* property = InGraph.FindProperty( id );
                OutOutputs.push_back( Pin( "Out", property ? property->Type : ValueType::Float ) );
            }

            std::string GetTitle( const Graph& InGraph, const Node& InNode ) const override
            {
                const Property* property = InGraph.FindProperty( InNode.Settings.value( "Property", std::string() ) );
                return property ? property->Name : "Missing Property";
            }

            void Emit( EmitContext& InContext ) const override
            {
                InContext.OutAlias( "Out", InContext.PropertyValue( InContext.SettingString( "Property" ) ) );
            }
        };


        // A texture used directly (not a property: it can't be changed per material).
        class TextureAssetNodeDef
            : public NodeDef
        {
        public:
            TextureAssetNodeDef( const char* InType, const char* InName, ValueType InTextureType )
                : m_type( InTextureType )
            {
                Type = InType;
                DisplayName = InName;
                Category = "Input/Texture";
                Keywords = "texture image asset sampler";
                Description = "A fixed texture (make a texture property instead to set it per material).";
                Outputs.push_back( Pin( "Out", InTextureType ) );
                Settings.push_back( MakeSetting( "Texture", SettingKind::Texture, "" ) );
            }

            void Emit( EmitContext& InContext ) const override
            {
                InContext.OutAlias( "Out", InContext.TextureAsset( InContext.SettingString( "Texture" ), m_type == ValueType::TextureCube ? "black" : "white", m_type ) );
            }

        private:
            ValueType m_type;
        };


        // One output whose expression is picked by an enum setting (spaces, constants, ...).
        class ChoiceNodeDef
            : public NodeDef
        {
        public:
            struct Choice
            {
                std::string Name;
                // Expression template (ExpressionNode syntax).
                std::string Expression;
            };

            ChoiceNodeDef( const char* InType, const char* InName, const char* InCategory, const char* InKeywords, const char* InSetting, ValueType InOutputType, std::vector<Choice> InChoices, const std::string& InDefault )
                : m_setting( InSetting )
                , m_choices( std::move( InChoices ) )
            {
                Type = InType;
                DisplayName = InName;
                Category = InCategory;
                Keywords = InKeywords;
                Outputs.push_back( Pin( "Out", InOutputType ) );
                std::vector<std::string> options;
                for( const Choice& choice : m_choices )
                {
                    options.push_back( choice.Name );
                }
                Settings.push_back( EnumSetting( InSetting, options, InDefault ) );
            }

            std::string GetTitle( const Graph&, const Node& InNode ) const override
            {
                return DisplayName + " (" + InNode.Settings.value( m_setting, Settings[0].Default.get<std::string>() ) + ")";
            }

            void Emit( EmitContext& InContext ) const override
            {
                const std::string value = InContext.SettingString( m_setting );
                const Choice* chosen = &m_choices[0];
                for( const Choice& choice : m_choices )
                {
                    if( choice.Name == value )
                    {
                        chosen = &choice;
                    }
                }
                InContext.OutAlias( "Out", ExpressionNode::Expand( InContext, *this, chosen->Expression ) );
            }

        private:
            std::string m_setting;
            std::vector<Choice> m_choices;
        };
    }


    void RegisterInputNodes( NodeRegistry& InRegistry )
    {
        InRegistry.Register( std::make_unique<ConstantNodeDef>( "Float", "Float", ValueType::Float, false ) );
        InRegistry.Register( std::make_unique<ConstantNodeDef>( "Vector2", "Vector 2", ValueType::Vec2, false ) );
        InRegistry.Register( std::make_unique<ConstantNodeDef>( "Vector3", "Vector 3", ValueType::Vec3, false ) );
        InRegistry.Register( std::make_unique<ConstantNodeDef>( "Vector4", "Vector 4", ValueType::Vec4, false ) );
        InRegistry.Register( std::make_unique<ConstantNodeDef>( "Color", "Color", ValueType::Vec4, true ) );
        InRegistry.Register( std::make_unique<BooleanNodeDef>() );
        InRegistry.Register( std::make_unique<PropertyNodeDef>() );
        InRegistry.Register( std::make_unique<TextureAssetNodeDef>( "Texture2DAsset", "Texture 2D Asset", ValueType::Texture2D ) );
        InRegistry.Register( std::make_unique<TextureAssetNodeDef>( "CubemapAsset", "Cubemap Asset", ValueType::TextureCube ) );

        using Choices = std::vector<ChoiceNodeDef::Choice>;
        InRegistry.Register( std::make_unique<ChoiceNodeDef>( "Constant", "Constant", "Input/Constants", "pi tau e phi golden sqrt2 math constant", "Value", ValueType::Float,
            Choices{ { "PI", "3.14159265359" }, { "TAU", "6.28318530718" }, { "PHI", "1.61803398875" }, { "E", "2.71828182846" }, { "SQRT2", "1.41421356237" } }, "PI" ) );

        InRegistry.Register( std::make_unique<ChoiceNodeDef>( "Position", "Position", "Input/Geometry", "vertex position world object view location", "Space", ValueType::Vec3,
            Choices{ { "World", "{B:PositionWorld}" }, { "Object", "{B:PositionObject}" }, { "View", "{B:PositionView}" } }, "World" ) );
        InRegistry.Register( std::make_unique<ChoiceNodeDef>( "NormalVector", "Normal Vector", "Input/Geometry", "normal surface direction", "Space", ValueType::Vec3,
            Choices{ { "World", "{B:NormalWorld}" }, { "Object", "{B:NormalObject}" }, { "Tangent", "vec3(0.0, 0.0, 1.0)" } }, "World" ) );
        InRegistry.Register( std::make_unique<ChoiceNodeDef>( "TangentVector", "Tangent Vector", "Input/Geometry", "tangent direction", "Space", ValueType::Vec3,
            Choices{ { "World", "{B:TangentWorld}" }, { "Tangent", "vec3(1.0, 0.0, 0.0)" } }, "World" ) );
        InRegistry.Register( std::make_unique<ChoiceNodeDef>( "BitangentVector", "Bitangent Vector", "Input/Geometry", "bitangent binormal direction", "Space", ValueType::Vec3,
            Choices{ { "World", "{B:BitangentWorld}" }, { "Tangent", "vec3(0.0, 1.0, 0.0)" } }, "World" ) );
        InRegistry.Register( std::make_unique<ChoiceNodeDef>( "ViewDirection", "View Direction", "Input/Geometry", "camera eye view vector direction", "Space", ValueType::Vec3,
            Choices{ { "World", "{B:ViewDirectionWorld}" }, { "Tangent", "sgWorldToTangent({B:ViewDirectionWorld}, {B:TangentWorld}, {B:BitangentWorld}, {B:NormalWorld})" } }, "World" ) );

        {
            ExpressionNode& node = InRegistry.Expression( "UV", "UV", "Input/Geometry" );
            node.Keywords = "texcoord texture coordinates uv0";
            node.Description = "The mesh's first texture coordinate set.";
            node.Outputs.push_back( Pin( "Out", ValueType::Vec2 ) );
            node.AliasOutputs = true;
            node.Expressions = { { "Out", "{B:UV0}" } };
        }
        {
            ExpressionNode& node = InRegistry.Expression( "Time", "Time", "Input/Basic" );
            node.Keywords = "seconds animate sine cosine delta";
            node.Outputs = { Pin( "Time", ValueType::Float ), Pin( "Sine Time", ValueType::Float ), Pin( "Cosine Time", ValueType::Float ), Pin( "Delta Time", ValueType::Float ) };
            node.AliasOutputs = true;
            node.Expressions = { { "Time", "{B:Time}" }, { "Sine Time", "sin({B:Time})" }, { "Cosine Time", "cos({B:Time})" }, { "Delta Time", "{B:DeltaTime}" } };
        }
        {
            ExpressionNode& node = InRegistry.Expression( "Camera", "Camera", "Input/Scene" );
            node.Keywords = "camera eye position forward direction";
            node.Outputs = { Pin( "Position", ValueType::Vec3 ), Pin( "Direction", ValueType::Vec3 ) };
            node.AliasOutputs = true;
            node.Expressions = { { "Position", "{B:CameraPosition}" }, { "Direction", "sgSafeNormalize(mul(u_invView, vec4(0.0, 0.0, 1.0, 0.0)).xyz)" } };
        }
        {
            ExpressionNode& node = InRegistry.Expression( "ScreenPosition", "Screen Position", "Input/Scene" );
            node.Keywords = "screen space pixel fragcoord viewport";
            node.Description = "Where the pixel is on the render target (fragment stage only).";
            node.Stages = StageFragment;
            node.Outputs = { Pin( "UV", ValueType::Vec2 ), Pin( "Pixel", ValueType::Vec2 ) };
            node.AliasOutputs = true;
            node.Expressions = { { "UV", "{B:ScreenUV}" }, { "Pixel", "{B:ScreenPixel}" } };
        }
        // Custom Lit (LOOK-S7): the scene's lights, for graphs that shade themselves.
        {
            ExpressionNode& node = InRegistry.Expression( "MainLight", "Main Light", "Input/Lighting" );
            node.Keywords = "sun directional light shadow toon cel ramp n dot l";
            node.Description = "The main (first directional) light: direction towards it, colour x intensity, its cascaded shadow (1 = lit) and N.L for a normal.";
            node.Stages = StageFragment;
            node.Targets = TargetBit( Target::CustomLit );
            node.Inputs = { Pin( "Normal", ValueType::Vec3 ).Bind( Builtin::NormalWorld ) };
            node.Outputs = { Pin( "Direction", ValueType::Vec3 ), Pin( "Color", ValueType::Vec3 ), Pin( "Shadow", ValueType::Float ), Pin( "N dot L", ValueType::Float ) };
            node.Expressions = { { "Direction", "sgMainLightDirection()" }, { "Color", "sgMainLightColor({B:PositionWorld})" },
                { "Shadow", "sgMainLightShadow({B:PositionWorld}, {Normal}, {B:PositionView}.z)" }, { "N dot L", "dot(sgSafeNormalize({Normal}), sgMainLightDirection())" } };
        }
        {
            ExpressionNode& node = InRegistry.Expression( "AdditionalLights", "Additional Lights", "Input/Lighting" );
            node.Keywords = "point spot local lights clustered toon cel ramp band";
            node.Description = "The point, spot and area lights at this pixel: each light's colour x attenuation x shadow by N.L (Diffuse), or by a smoothstep band around Threshold (Banded: cel shading).";
            node.Stages = StageFragment;
            node.Targets = TargetBit( Target::CustomLit );
            node.Inputs = { Pin( "Normal", ValueType::Vec3 ).Bind( Builtin::NormalWorld ), Pin( "Threshold", ValueType::Float, Vector4( 0.f ) ), Pin( "Smoothness", ValueType::Float, Vector4( 0.05f ) ) };
            node.Outputs = { Pin( "Diffuse", ValueType::Vec3 ), Pin( "Banded", ValueType::Vec3 ) };
            node.Expressions = { { "Diffuse", "sgAdditionalLights({B:PositionWorld}, {Normal}, {B:ScreenPixel}, {B:PositionView}.z, 0.0, 0.0, 1.0)" },
                { "Banded", "sgAdditionalLights({B:PositionWorld}, {Normal}, {B:ScreenPixel}, {B:PositionView}.z, 1.0, {Threshold}, {Smoothness})" } };
        }
        {
            ExpressionNode& node = InRegistry.Expression( "AmbientLight", "Ambient Light", "Input/Lighting" );
            node.Keywords = "ambient sky irradiance environment fill";
            node.Description = "The ambient light (sky / environment irradiance) arriving along a normal.";
            node.Stages = StageFragment;
            node.Targets = TargetBit( Target::CustomLit );
            node.Inputs = { Pin( "Normal", ValueType::Vec3 ).Bind( Builtin::NormalWorld ) };
            node.Outputs = { Pin( "Color", ValueType::Vec3 ) };
            node.Expressions = { { "Color", "sgAmbientLight({Normal})" } };
        }
        {
            ExpressionNode& node = InRegistry.Expression( "SceneDepth", "Scene Depth", "Input/Scene" );
            node.Keywords = "depth buffer distance z soft intersection";
            node.Description = "Linear view depth (distance along the view) of the opaque scene under a screen position.";
            node.Stages = StageFragment;
            node.Targets = TargetBit( Target::Particle ) | TargetBit( Target::Decal ) | TargetBit( Target::PostProcess );
            node.Inputs = { Pin( "UV", ValueType::Vec2 ).Bind( Builtin::ScreenUV ) };
            node.Outputs = { Pin( "Out", ValueType::Float ) };
            node.Expressions = { { "Out", "sgSceneDepth({UV})" } };
        }
        {
            ExpressionNode& node = InRegistry.Expression( "SceneColor", "Scene Color", "Input/Scene" );
            node.Keywords = "screen color frame buffer grab distortion";
            node.Description = "The rendered image under a screen position (HDR before tonemapping, display colour after).";
            node.Stages = StageFragment;
            node.Targets = TargetBit( Target::PostProcess );
            node.Inputs = { Pin( "UV", ValueType::Vec2 ).Bind( Builtin::ScreenUV ) };
            node.Outputs = { Pin( "Out", ValueType::Vec3 ) };
            node.Expressions = { { "Out", "sgSceneColor({UV})" } };
        }
        {
            ExpressionNode& node = InRegistry.Expression( "Particle", "Particle", "Input/Particle" );
            node.Keywords = "particle colour color rotation velocity quad uv";
            node.Description = "The particle's colour (with alpha, over its life), its own quad UV, rotation (radians) and velocity.";
            node.Targets = TargetBit( Target::Particle );
            node.Outputs = { Pin( "Color", ValueType::Vec4 ), Pin( "Quad UV", ValueType::Vec2 ), Pin( "Rotation", ValueType::Float ), Pin( "Velocity", ValueType::Vec3 ) };
            node.AliasOutputs = true;
            node.Expressions = { { "Color", "{B:VertexColor}" }, { "Quad UV", "{B:ParticleQuadUV}" }, { "Rotation", "{B:ParticleRotation}" }, { "Velocity", "{B:ParticleVelocity}" } };
        }
        {
            ExpressionNode& node = InRegistry.Expression( "ParticleTexture", "Particle Texture", "Input/Particle" );
            node.Keywords = "particle texture sprite main";
            node.Description = "The ParticleSystem's Texture (sample it with UV for the flipbook cell).";
            node.Targets = TargetBit( Target::Particle );
            node.Outputs = { Pin( "Out", ValueType::Texture2D ) };
            node.AliasOutputs = true;
            node.Expressions = { { "Out", "sgParticleTexture" } };
        }
        {
            ExpressionNode& node = InRegistry.Expression( "Decal", "Decal", "Input/Decal" );
            node.Keywords = "decal fade box projection";
            node.Description = "The decal's angle fade and the position inside its box (-0.5..0.5).";
            node.Targets = TargetBit( Target::Decal );
            node.Outputs = { Pin( "Fade", ValueType::Float ), Pin( "Box Position", ValueType::Vec3 ) };
            node.AliasOutputs = true;
            node.Expressions = { { "Fade", "{B:DecalFade}" }, { "Box Position", "{B:PositionObject}" } };
        }
        {
            ExpressionNode& node = InRegistry.Expression( "IsFrontFace", "Is Front Face", "Input/Geometry" );
            node.Keywords = "backface two sided double";
            node.Stages = StageFragment;
            node.Outputs = { Pin( "Out", ValueType::Bool ) };
            node.AliasOutputs = true;
            node.Expressions = { { "Out", "{B:FrontFacing}" } };
        }
    }
}
