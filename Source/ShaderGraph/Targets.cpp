#include "PCH.h"
#include "Targets.h"
#include "Path.h"

namespace ShaderGraph
{
    namespace
    {
        using BuiltinTable = std::map<Builtin, BuiltinSource>;

        // Mesh templates (Mesh.vert.sgt, Lit/Unlit/Shadow.frag.sgt). The vertex template has
        // sg_model and sg_normalMatrix; the fragment templates read the Mesh.var varyings.
        const BuiltinTable& MeshVertexBuiltins()
        {
            static const BuiltinTable table = {
                { Builtin::UV0, { "vec2 sg_UV0 = a_texcoord0;", {} } },
                { Builtin::PositionObject, { "vec3 sg_PositionObject = a_position;", {} } },
                { Builtin::PositionWorld, { "vec3 sg_PositionWorld = mul(sg_model, vec4(a_position, 1.0)).xyz;", {} } },
                { Builtin::PositionView, { "vec3 sg_PositionView = mul(u_view, vec4(sg_PositionWorld, 1.0)).xyz;", { Builtin::PositionWorld } } },
                { Builtin::NormalObject, { "vec3 sg_NormalObject = a_normal;", {} } },
                { Builtin::NormalWorld, { "vec3 sg_NormalWorld = sgSafeNormalize(mul(sg_normalMatrix, a_normal));", {} } },
                { Builtin::TangentWorld, { "vec3 sg_TangentWorld = sgSafeNormalize(mul(sg_model, vec4(a_tangent, 0.0)).xyz);", {} } },
                { Builtin::BitangentWorld, { "vec3 sg_BitangentWorld = sgSafeNormalize(mul(sg_model, vec4(a_bitangent, 0.0)).xyz);", {} } },
                { Builtin::CameraPosition, { "vec3 sg_CameraPosition = mul(u_invView, vec4(0.0, 0.0, 0.0, 1.0)).xyz;", {} } },
                { Builtin::ViewDirectionWorld, { "vec3 sg_ViewDirectionWorld = sgSafeNormalize(sg_CameraPosition - sg_PositionWorld);", { Builtin::CameraPosition, Builtin::PositionWorld } } },
                { Builtin::Time, { "float sg_Time = u_time.y;", {} } },
                { Builtin::DeltaTime, { "float sg_DeltaTime = u_time.x;", {} } },
            };
            return table;
        }


        const BuiltinTable& MeshFragmentBuiltins()
        {
            static const BuiltinTable table = {
                { Builtin::UV0, { "vec2 sg_UV0 = v_texcoord0;", {} } },
                { Builtin::PositionObject, { "vec3 sg_PositionObject = v_objectPos;", {} } },
                { Builtin::PositionWorld, { "vec3 sg_PositionWorld = v_worldPos;", {} } },
                { Builtin::PositionView, { "vec3 sg_PositionView = v_viewPos.xyz;", {} } },
                { Builtin::NormalObject, { "vec3 sg_NormalObject = sgSafeNormalize(v_objectNormal);", {} } },
                { Builtin::NormalWorld, { "vec3 sg_NormalWorld = sgSafeNormalize(v_normal);", {} } },
                { Builtin::TangentWorld, { "vec3 sg_TangentWorld = sgSafeNormalize(v_tangent);", {} } },
                { Builtin::BitangentWorld, { "vec3 sg_BitangentWorld = sgSafeNormalize(v_bitangent);", {} } },
                { Builtin::CameraPosition, { "vec3 sg_CameraPosition = mul(u_invView, vec4(0.0, 0.0, 0.0, 1.0)).xyz;", {} } },
                { Builtin::ViewDirectionWorld, { "vec3 sg_ViewDirectionWorld = sgSafeNormalize(sg_CameraPosition - v_worldPos);", { Builtin::CameraPosition } } },
                { Builtin::ScreenPixel, { "vec2 sg_ScreenPixel = sg_fragCoord.xy;", {} } },
                { Builtin::ScreenUV, { "vec2 sg_ScreenUV = sg_fragCoord.xy * u_viewTexel.xy;", {} } },
                { Builtin::FrontFacing, { "bool sg_FrontFacing = sg_frontFacing;", {} } },
                { Builtin::Time, { "float sg_Time = u_time.y;", {} } },
                { Builtin::DeltaTime, { "float sg_DeltaTime = u_time.x;", {} } },
            };
            return table;
        }


        const BuiltinSource* FindBuiltin( const BuiltinTable& InTable, Builtin InBuiltin )
        {
            auto found = InTable.find( InBuiltin );
            return found == InTable.end() ? nullptr : &found->second;
        }


        // Lit and Unlit: meshes through ShaderGraphMaterial (instanced, skinned and shadow variants).
        class MeshTargetDef
            : public TargetDef
        {
        public:
            const BuiltinSource* GetBuiltin( Builtin InBuiltin, Stage InStage ) const override
            {
                return FindBuiltin( InStage == Stage::Vertex ? MeshVertexBuiltins() : MeshFragmentBuiltins(), InBuiltin );
            }

            void GetDefines( const TargetSettings& InSettings, std::vector<std::string>& OutDefines ) const override
            {
                TargetDef::GetDefines( InSettings, OutDefines );
                if( InSettings.OffsetSpace == PositionSpace::World )
                {
                    OutDefines.push_back( "SG_OFFSET_WORLD 1" );
                }
            }

            void GetFiles( const TargetSettings& InSettings, const GraphFacts& InFacts, std::vector<TemplateFile>& OutFiles, json& OutPrograms ) const override
            {
                const std::string skinInputs = ", a_indices, a_weight";
                OutFiles.push_back( { "Forward.var", "Mesh.var", TemplateFile::Kind::Varyings } );
                OutFiles.push_back( { "Skinned.var", "Mesh.var", TemplateFile::Kind::Varyings } );
                OutFiles.push_back( { "Forward.vert", "Mesh.vert.sgt", TemplateFile::Kind::Vertex, {}, {}, { { "SKIN_INPUTS", "" } } } );
                OutFiles.push_back( { "Skinned.vert", "Mesh.vert.sgt", TemplateFile::Kind::Vertex, {}, { "SG_SKINNED 1" }, { { "SKIN_INPUTS", skinInputs } } } );
                OutFiles.push_back( { "Forward.frag", FragmentTemplate(), TemplateFile::Kind::Fragment } );
                OutPrograms["Forward"] = json::array( { "Forward", "Forward" } );
                OutPrograms["Skinned"] = json::array( { "Skinned", "Forward" } );

                // Shadows only need their own program when the graph moves vertices or cuts holes;
                // otherwise the renderer's depth-only caster draws the same silhouette, cheaper.
                const bool offset = InFacts.IsOutputConnected && InFacts.IsOutputConnected( "Position Offset" );
                if( InSettings.CastShadows && ( offset || InSettings.AlphaClip ) )
                {
                    OutFiles.push_back( { "Shadow.var", "Mesh.var", TemplateFile::Kind::Varyings } );
                    OutFiles.push_back( { "Shadow.frag", "Shadow.frag.sgt", TemplateFile::Kind::Fragment, { "Alpha", "Alpha Clip Threshold" } } );
                    OutPrograms["Shadow"] = json::array( { "Forward", "Shadow" } );
                    OutPrograms["ShadowSkinned"] = json::array( { "Skinned", "Shadow" } );
                }
            }

        protected:
            virtual std::string FragmentTemplate() const = 0;

            void AddVertexPins( std::vector<PinDef>& OutPins ) const
            {
                OutPins.push_back( PinDef( "Position Offset", ValueType::Vec3 ).StageOnly( StageVertex ).Tip( "Moves vertices (object or world space, see Offset Space). Shadows follow." ) );
            }

            void AddAlphaPins( const TargetSettings& InSettings, std::vector<PinDef>& OutPins ) const
            {
                PinDef alpha( "Alpha", ValueType::Float, Vector4( 1.f ) );
                alpha.StageOnly( StageFragment ).Tip( "Opacity (transparent surfaces) and the value alpha clipping tests." );
                OutPins.push_back( alpha );
                PinDef threshold( "Alpha Clip Threshold", ValueType::Float, Vector4( 0.5f ) );
                threshold.StageOnly( StageFragment ).Tip( "Fragments with Alpha below this are discarded (needs Alpha Clip)." );
                threshold.Hidden = !InSettings.AlphaClip;
                OutPins.push_back( threshold );
            }
        };


        class LitTargetDef
            : public MeshTargetDef
        {
        public:
            LitTargetDef()
            {
                Id = Target::Lit;
                DisplayName = "Lit";
                Description = "A metallic-roughness surface with the engine's full lighting: sun shadows, clustered lights, image-based ambient and fog.";
            }

            void BuildOutputPins( const TargetSettings& InSettings, std::vector<PinDef>& OutPins ) const override
            {
                AddVertexPins( OutPins );
                OutPins.push_back( PinDef( "Base Color", ValueType::Vec3, Vector4( 0.5f, 0.5f, 0.5f, 1.f ) ).Color().StageOnly( StageFragment ).Tip( "Linear albedo (metals: the specular colour)." ) );
                if( InSettings.FragmentNormalSpace == NormalSpace::World )
                {
                    OutPins.push_back( PinDef( "Normal", ValueType::Vec3 ).Bind( Builtin::NormalWorld ).StageOnly( StageFragment ).Tip( "World-space shading normal." ) );
                }
                else
                {
                    OutPins.push_back( PinDef( "Normal", ValueType::Vec3, Vector4( 0.f, 0.f, 1.f, 0.f ) ).StageOnly( StageFragment ).Tip( "Tangent-space shading normal (a normal map's unpacked value)." ) );
                }
                OutPins.push_back( PinDef( "Metallic", ValueType::Float ).StageOnly( StageFragment ) );
                OutPins.push_back( PinDef( "Roughness", ValueType::Float, Vector4( 0.5f ) ).StageOnly( StageFragment ).Tip( "Perceptual roughness (0 mirror, 1 matte)." ) );
                OutPins.push_back( PinDef( "Emission", ValueType::Vec3 ).Color().StageOnly( StageFragment ).Tip( "Linear emitted radiance (HDR: values above 1 bloom)." ) );
                OutPins.push_back( PinDef( "Occlusion", ValueType::Float, Vector4( 1.f ) ).StageOnly( StageFragment ).Tip( "Ambient occlusion (1 unoccluded)." ) );
                AddAlphaPins( InSettings, OutPins );
            }

        protected:
            std::string FragmentTemplate() const override
            {
                return "Lit.frag.sgt";
            }
        };


        class UnlitTargetDef
            : public MeshTargetDef
        {
        public:
            UnlitTargetDef()
            {
                Id = Target::Unlit;
                DisplayName = "Unlit";
                Description = "Emitted colour only (still fogged and HDR).";
            }

            void BuildOutputPins( const TargetSettings& InSettings, std::vector<PinDef>& OutPins ) const override
            {
                AddVertexPins( OutPins );
                OutPins.push_back( PinDef( "Color", ValueType::Vec3, Vector4( 0.5f, 0.5f, 0.5f, 1.f ) ).Color().StageOnly( StageFragment ).Tip( "Linear HDR colour." ) );
                AddAlphaPins( InSettings, OutPins );
            }

            void GetDefines( const TargetSettings& InSettings, std::vector<std::string>& OutDefines ) const override
            {
                MeshTargetDef::GetDefines( InSettings, OutDefines );
                if( InSettings.Fog )
                {
                    OutDefines.push_back( "SG_FOG 1" );
                }
            }

        protected:
            std::string FragmentTemplate() const override
            {
                return "Unlit.frag.sgt";
            }
        };
    }


    namespace
    {
        // Particles: the engine's Particle.vert draws the quads; the graph colours them.
        class ParticleTargetDef
            : public TargetDef
        {
        public:
            ParticleTargetDef()
            {
                Id = Target::Particle;
                DisplayName = "Particle";
                Description = "Colour and alpha of a ParticleSystem's quads (set the system's Graph). The system still does depth fade, lighting (Lit), fog and blending.";
                HasSceneDepth = true;
                // Stage 6: the system's texture, 7: the scene depth.
                MaxTextures = 6;
            }

            void BuildOutputPins( const TargetSettings&, std::vector<PinDef>& OutPins ) const override
            {
                OutPins.push_back( PinDef( "Color", ValueType::Vec3, Vector4( 1.f, 1.f, 1.f, 1.f ) ).Color().StageOnly( StageFragment ).Tip( "Linear HDR colour (multiply by the particle colour for the system's colour over life)." ) );
                OutPins.push_back( PinDef( "Alpha", ValueType::Float, Vector4( 1.f ) ).StageOnly( StageFragment ) );
            }

            const BuiltinSource* GetBuiltin( Builtin InBuiltin, Stage InStage ) const override
            {
                static const BuiltinTable table = {
                    { Builtin::UV0, { "vec2 sg_UV0 = v_texcoord0;", {} } },
                    { Builtin::ParticleQuadUV, { "vec2 sg_ParticleQuadUV = v_particleData.zw;", {} } },
                    { Builtin::VertexColor, { "vec4 sg_VertexColor = v_color0;", {} } },
                    { Builtin::ParticleRotation, { "float sg_ParticleRotation = v_particleData.x;", {} } },
                    { Builtin::ParticleVelocity, { "vec3 sg_ParticleVelocity = v_particleVelocity;", {} } },
                    { Builtin::PositionWorld, { "vec3 sg_PositionWorld = v_worldPos;", {} } },
                    { Builtin::PositionView, { "vec3 sg_PositionView = v_viewPos.xyz;", {} } },
                    { Builtin::CameraPosition, { "vec3 sg_CameraPosition = mul(u_invView, vec4(0.0, 0.0, 0.0, 1.0)).xyz;", {} } },
                    { Builtin::ViewDirectionWorld, { "vec3 sg_ViewDirectionWorld = sgSafeNormalize(sg_CameraPosition - v_worldPos);", { Builtin::CameraPosition } } },
                    // Camera-facing quads: the normal points back at the camera.
                    { Builtin::NormalWorld, { "vec3 sg_NormalWorld = sgSafeNormalize(mul(u_invView, vec4(0.0, 0.0, -1.0, 0.0)).xyz);", {} } },
                    { Builtin::TangentWorld, { "vec3 sg_TangentWorld = sgSafeNormalize(mul(u_invView, vec4(1.0, 0.0, 0.0, 0.0)).xyz);", {} } },
                    { Builtin::BitangentWorld, { "vec3 sg_BitangentWorld = sgSafeNormalize(mul(u_invView, vec4(0.0, 1.0, 0.0, 0.0)).xyz);", {} } },
                    { Builtin::FrontFacing, { "bool sg_FrontFacing = true;", {} } },
                    { Builtin::ScreenPixel, { "vec2 sg_ScreenPixel = sg_fragCoord.xy;", {} } },
                    { Builtin::ScreenUV, { "vec2 sg_ScreenUV = sg_fragCoord.xy * u_viewTexel.xy;", {} } },
                    { Builtin::Time, { "float sg_Time = u_time.y;", {} } },
                    { Builtin::DeltaTime, { "float sg_DeltaTime = u_time.x;", {} } },
                };
                return InStage == Stage::Fragment ? FindBuiltin( table, InBuiltin ) : nullptr;
            }

            void GetDefines( const TargetSettings& InSettings, std::vector<std::string>& OutDefines ) const override
            {
                OutDefines.push_back( "SG_TARGET_PARTICLE 1" );
            }

            void GetFiles( const TargetSettings&, const GraphFacts&, std::vector<TemplateFile>& OutFiles, json& OutPrograms ) const override
            {
                // The fragment shader's varyings are the engine particle shader's (kept in sync by
                // copying its .var).
                OutFiles.push_back( { "Particle.var", "../../Particle.var", TemplateFile::Kind::Varyings } );
                OutFiles.push_back( { "Particle.frag", "Particle.frag.sgt", TemplateFile::Kind::Fragment } );
                OutPrograms["Forward"] = json::array( { "@Assets/Shaders/Particle", "Particle" } );
            }
        };


        // Decals: the engine's Decal.vert draws the box; the graph is the decal's surface.
        class DecalTargetDef
            : public TargetDef
        {
        public:
            DecalTargetDef()
            {
                Id = Target::Decal;
                DisplayName = "Decal";
                Description = "The surface a Decal projects (set the decal's Graph): lit like the scene, faded by the decal's angle settings.";
                HasSceneDepth = true;
                // Stage 7: the scene depth.
                MaxTextures = 7;
            }

            void BuildOutputPins( const TargetSettings&, std::vector<PinDef>& OutPins ) const override
            {
                OutPins.push_back( PinDef( "Base Color", ValueType::Vec3, Vector4( 0.8f, 0.8f, 0.8f, 1.f ) ).Color().StageOnly( StageFragment ) );
                OutPins.push_back( PinDef( "Alpha", ValueType::Float, Vector4( 1.f ) ).StageOnly( StageFragment ).Tip( "Coverage (multiplied by the angle fade)." ) );
                OutPins.push_back( PinDef( "Normal", ValueType::Vec3, Vector4( 0.f, 0.f, 1.f, 0.f ) ).StageOnly( StageFragment ).Tip( "Tangent-space normal: x along the box's X, y along its Y." ) );
                OutPins.push_back( PinDef( "Metallic", ValueType::Float ).StageOnly( StageFragment ) );
                OutPins.push_back( PinDef( "Roughness", ValueType::Float, Vector4( 0.5f ) ).StageOnly( StageFragment ) );
                OutPins.push_back( PinDef( "Emission", ValueType::Vec3 ).Color().StageOnly( StageFragment ) );
            }

            const BuiltinSource* GetBuiltin( Builtin InBuiltin, Stage InStage ) const override
            {
                static const BuiltinTable table = {
                    { Builtin::UV0, { "vec2 sg_UV0 = sgDecal.uv;", {} } },
                    { Builtin::PositionObject, { "vec3 sg_PositionObject = sgDecal.local;", {} } },
                    { Builtin::PositionWorld, { "vec3 sg_PositionWorld = sgDecal.worldPos;", {} } },
                    { Builtin::PositionView, { "vec3 sg_PositionView = sgDecal.viewPos;", {} } },
                    { Builtin::NormalWorld, { "vec3 sg_NormalWorld = sgDecal.normal;", {} } },
                    { Builtin::TangentWorld, { "vec3 sg_TangentWorld = sgDecal.tangent;", {} } },
                    { Builtin::BitangentWorld, { "vec3 sg_BitangentWorld = sgDecal.bitangent;", {} } },
                    { Builtin::ViewDirectionWorld, { "vec3 sg_ViewDirectionWorld = sgDecal.view;", {} } },
                    { Builtin::CameraPosition, { "vec3 sg_CameraPosition = mul(u_invView, vec4(0.0, 0.0, 0.0, 1.0)).xyz;", {} } },
                    { Builtin::DecalFade, { "float sg_DecalFade = sgDecal.fade;", {} } },
                    { Builtin::FrontFacing, { "bool sg_FrontFacing = true;", {} } },
                    { Builtin::ScreenPixel, { "vec2 sg_ScreenPixel = sg_fragCoord.xy;", {} } },
                    { Builtin::ScreenUV, { "vec2 sg_ScreenUV = (sg_fragCoord.xy - u_viewRect.xy) / u_viewRect.zw;", {} } },
                    { Builtin::Time, { "float sg_Time = u_time.y;", {} } },
                    { Builtin::DeltaTime, { "float sg_DeltaTime = u_time.x;", {} } },
                };
                return InStage == Stage::Fragment ? FindBuiltin( table, InBuiltin ) : nullptr;
            }

            void GetDefines( const TargetSettings&, std::vector<std::string>& OutDefines ) const override
            {
                OutDefines.push_back( "SG_TARGET_DECAL 1" );
            }

            void GetFiles( const TargetSettings&, const GraphFacts&, std::vector<TemplateFile>& OutFiles, json& OutPrograms ) const override
            {
                OutFiles.push_back( { "Decal.var", "../../Decal.var", TemplateFile::Kind::Varyings } );
                OutFiles.push_back( { "Decal.frag", "Decal.frag.sgt", TemplateFile::Kind::Fragment } );
                OutPrograms["Forward"] = json::array( { "@Assets/Shaders/Decal", "Decal" } );
            }
        };
    }


    namespace
    {
        // Full-screen passes on the camera's image (PostProcess.Graphs).
        class PostProcessTargetDef
            : public TargetDef
        {
        public:
            PostProcessTargetDef()
            {
                Id = Target::PostProcess;
                DisplayName = "Post Process";
                Description = "A full-screen pass on the camera's image (add it to a PostProcess component's Graphs). Before tonemapping it works on HDR scene light; after, on the display image.";
                HasSceneColor = true;
                HasSceneDepth = true;
                // Stage 6: the scene colour, 7: the scene depth.
                MaxTextures = 6;
            }

            void BuildOutputPins( const TargetSettings&, std::vector<PinDef>& OutPins ) const override
            {
                OutPins.push_back( PinDef( "Color", ValueType::Vec3 ).Bind( Builtin::SceneColorInput ).Color().StageOnly( StageFragment ).Tip( "The new image (unconnected: unchanged)." ) );
            }

            const BuiltinSource* GetBuiltin( Builtin InBuiltin, Stage InStage ) const override
            {
                static const BuiltinTable table = {
                    { Builtin::UV0, { "vec2 sg_UV0 = v_texcoord0;", {} } },
                    { Builtin::ScreenUV, { "vec2 sg_ScreenUV = v_texcoord0;", {} } },
                    { Builtin::ScreenPixel, { "vec2 sg_ScreenPixel = sg_fragCoord.xy;", {} } },
                    { Builtin::SceneColorInput, { "vec3 sg_SceneColorInput = sgSceneColor(v_texcoord0);", {} } },
                    { Builtin::PositionView, { "vec3 sg_PositionView = sgViewPosition(v_texcoord0);", {} } },
                    { Builtin::PositionWorld, { "vec3 sg_PositionWorld = mul(u_sgPostInvView, vec4(sg_PositionView, 1.0)).xyz;", { Builtin::PositionView } } },
                    { Builtin::CameraPosition, { "vec3 sg_CameraPosition = mul(u_sgPostInvView, vec4(0.0, 0.0, 0.0, 1.0)).xyz;", {} } },
                    { Builtin::ViewDirectionWorld, { "vec3 sg_ViewDirectionWorld = sgSafeNormalize(sg_CameraPosition - sg_PositionWorld);", { Builtin::CameraPosition, Builtin::PositionWorld } } },
                    { Builtin::NormalWorld, { "vec3 sg_NormalWorld = sgSafeNormalize(mul(u_sgPostInvView, vec4(sgViewNormal(v_texcoord0), 0.0)).xyz);", {} } },
                    { Builtin::TangentWorld, { "vec3 sg_TangentWorld = sgSafeNormalize(cross(abs(sg_NormalWorld.y) < 0.99 ? vec3(0.0, 1.0, 0.0) : vec3(1.0, 0.0, 0.0), sg_NormalWorld));", { Builtin::NormalWorld } } },
                    { Builtin::BitangentWorld, { "vec3 sg_BitangentWorld = cross(sg_NormalWorld, sg_TangentWorld);", { Builtin::NormalWorld, Builtin::TangentWorld } } },
                    { Builtin::FrontFacing, { "bool sg_FrontFacing = true;", {} } },
                    { Builtin::Time, { "float sg_Time = u_time.y;", {} } },
                    { Builtin::DeltaTime, { "float sg_DeltaTime = u_time.x;", {} } },
                };
                return InStage == Stage::Fragment ? FindBuiltin( table, InBuiltin ) : nullptr;
            }

            void GetDefines( const TargetSettings&, std::vector<std::string>& OutDefines ) const override
            {
                OutDefines.push_back( "SG_TARGET_POST 1" );
            }

            void GetFiles( const TargetSettings&, const GraphFacts&, std::vector<TemplateFile>& OutFiles, json& OutPrograms ) const override
            {
                OutFiles.push_back( { "Post.var", "../../Post/Fullscreen.var", TemplateFile::Kind::Varyings } );
                OutFiles.push_back( { "Post.frag", "PostProcess.frag.sgt", TemplateFile::Kind::Fragment } );
                OutPrograms["Forward"] = json::array( { "@Assets/Shaders/Post/Fullscreen", "Post" } );
            }
        };
    }


    void TargetDef::GetDefines( const TargetSettings& InSettings, std::vector<std::string>& OutDefines ) const
    {
        if( InSettings.AlphaClip )
        {
            OutDefines.push_back( "SG_ALPHA_CLIP 1" );
        }
        if( InSettings.FragmentNormalSpace == NormalSpace::World )
        {
            OutDefines.push_back( "SG_NORMAL_WORLD 1" );
        }
        if( InSettings.Surface == SurfaceType::Transparent )
        {
            OutDefines.push_back( "SG_TRANSPARENT 1" );
        }
    }


    const TargetDef* GetTargetDef( Target InTarget )
    {
        static const LitTargetDef lit;
        static const UnlitTargetDef unlit;
        static const ParticleTargetDef particle;
        static const DecalTargetDef decal;
        static const PostProcessTargetDef post;
        switch( InTarget )
        {
        case Target::Lit:
            return &lit;
        case Target::Unlit:
            return &unlit;
        case Target::Particle:
            return &particle;
        case Target::Decal:
            return &decal;
        case Target::PostProcess:
            return &post;
        default:
            return nullptr;
        }
    }


    const BuiltinSource* GetPreviewBuiltin( Builtin InBuiltin )
    {
        // Template locals: sgN (surface normal), sgP (position), sgSphere, v_texcoord0.
        static const BuiltinTable table = {
            { Builtin::UV0, { "vec2 sg_UV0 = sgSphere ? vec2(atan2(sgN.x, sgN.z) * 0.15915494 + 0.5, acos(clamp(sgN.y, -1.0, 1.0)) * 0.31830989) : v_texcoord0;", {} } },
            { Builtin::PositionObject, { "vec3 sg_PositionObject = sgP;", {} } },
            { Builtin::PositionWorld, { "vec3 sg_PositionWorld = sgP;", {} } },
            { Builtin::PositionView, { "vec3 sg_PositionView = sgP - vec3(0.0, 0.0, 2.5);", {} } },
            { Builtin::NormalObject, { "vec3 sg_NormalObject = sgN;", {} } },
            { Builtin::NormalWorld, { "vec3 sg_NormalWorld = sgN;", {} } },
            { Builtin::TangentWorld, { "vec3 sg_TangentWorld = sgSafeNormalize(vec3(sgN.z, 0.0, -sgN.x));", {} } },
            { Builtin::BitangentWorld, { "vec3 sg_BitangentWorld = cross(sgN, sg_TangentWorld);", { Builtin::TangentWorld } } },
            { Builtin::ViewDirectionWorld, { "vec3 sg_ViewDirectionWorld = vec3(0.0, 0.0, 1.0);", {} } },
            { Builtin::CameraPosition, { "vec3 sg_CameraPosition = vec3(0.0, 0.0, 2.5);", {} } },
            { Builtin::ScreenUV, { "vec2 sg_ScreenUV = v_texcoord0;", {} } },
            { Builtin::ScreenPixel, { "vec2 sg_ScreenPixel = v_texcoord0 * 256.0;", {} } },
            { Builtin::Time, { "float sg_Time = u_time.y;", {} } },
            { Builtin::DeltaTime, { "float sg_DeltaTime = u_time.x;", {} } },
            { Builtin::VertexColor, { "vec4 sg_VertexColor = vec4(1.0, 1.0, 1.0, 1.0);", {} } },
            { Builtin::FrontFacing, { "bool sg_FrontFacing = true;", {} } },
            { Builtin::ParticleRotation, { "float sg_ParticleRotation = 0.0;", {} } },
            { Builtin::ParticleVelocity, { "vec3 sg_ParticleVelocity = vec3(0.0, 1.0, 0.0);", {} } },
            { Builtin::DecalFade, { "float sg_DecalFade = 1.0;", {} } },
            { Builtin::ParticleQuadUV, { "vec2 sg_ParticleQuadUV = v_texcoord0;", {} } },
            { Builtin::SceneColorInput, { "vec3 sg_SceneColorInput = sgSceneColor(v_texcoord0);", {} } },
        };
        return FindBuiltin( table, InBuiltin );
    }


    std::string GetTemplateDirectory()
    {
        return Path( "Engine/Assets/Shaders/ShaderGraph/Templates" ).FullPath;
    }
}
