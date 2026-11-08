#include "CameraInspector.h"

#if USING( ME_EDITOR )
#include "Editor/ReflectionUI.h"
#include "Components/Camera.h"
#include "Components/Graphics/PostProcess.h"
#include "Camera/CameraData.h"
#include "Renderer.h"
#include <Engine/Engine.h>
#include <algorithm>
#include <cmath>
#include <string>
#include <Widgets/CameraWidgets.h>

namespace CameraInspector
{
    namespace
    {
        template<typename T>
        T& As( void* InInstance )
        {
            return *static_cast<T*>( static_cast<BaseComponent*>( InInstance ) );
        }

        // The field of view is private (m_FOV): reach it through its reflected field.
        float* FovOf( void* InInstance, const Reflection::TypeInfo& InType )
        {
            const Reflection::FieldInfo* field = InType.FindField( "FieldOfView" );
            return field ? static_cast<float*>( field->Get( InInstance ) ) : nullptr;
        }

        // The sensor height a camera's lens is measured against: its PostProcess filmback when it
        // has one (what depth of field uses), else full frame.
        double SensorHeight( const Camera& InCamera, const char*& OutNote )
        {
            if( PostProcess* post = InCamera.Parent ? InCamera.Parent->TryGetComponent<PostProcess>() : nullptr )
            {
                OutNote = "from this camera's PostProcess Sensor Height";
                return std::clamp( post->DoFSensorHeight, 1.f, 70.f );
            }
            OutNote = "full frame; add a PostProcess to set the Sensor Height";
            return MeWidgets::Lens::kFullFrameHeight;
        }

        // The lens a PostProcess's depth of field sees: the camera on its entity, else the main one.
        double FocalLength( const PostProcess& InPost, const char*& OutNote )
        {
            Camera* camera = InPost.Parent ? InPost.Parent->TryGetComponent<Camera>() : nullptr;
            OutNote = "the focal length from this camera's field of view and Sensor Height";
            if( !camera )
            {
                camera = InPost.Parent ? Camera::GetMain( InPost.Parent->GetWorld() ) : nullptr;
                OutNote = "the focal length from the main camera's field of view and Sensor Height";
            }
            const double fov = camera ? camera->GetFOV() : 60.0;
            return MeWidgets::Lens::FocalLength( fov, std::clamp( InPost.DoFSensorHeight, 1.f, 70.f ) );
        }

        // The size a camera's image is shown at, and where that comes from. Only the main camera,
        // render texture cameras and the ones sized from the main camera (CameraCore) keep their
        // OutputSize current; any other camera's is stale, so it reads what it would get once current.
        Vector2 OutputSize( const Camera& InCamera, const char*& OutNote )
        {
            if( InCamera.RendersToTexture() )
            {
                OutNote = "its Target Texture's size";
                return InCamera.GetViewportSize();
            }
            if( InCamera.IsMain() && !InCamera.OutputSize.IsZero() )
            {
                OutNote = InCamera.HasPartialViewport() ? "its viewport rect of the Game View" : "the Game View size";
                return InCamera.GetViewportSize();
            }
            const bool sized = InCamera.MatchMainCamera || InCamera.HasPartialViewport();
            OutNote = !sized ? "the Game View size, when this camera is current" : InCamera.HasPartialViewport() ? "its viewport rect of the Game View" : "the Game View size, matching the main camera";
            const Camera* main = InCamera.Parent ? Camera::GetMain( InCamera.Parent->GetWorld() ) : nullptr;
            const Vector2 game = main && !main->OutputSize.IsZero() ? main->OutputSize : Vector2( 1920.f, 1080.f );
            const float width = std::clamp( InCamera.Viewport.z, 0.01f, 1.f );
            const float height = std::clamp( InCamera.Viewport.w, 0.01f, 1.f );
            return Vector2( std::max( 1.f, std::round( game.x * width ) ), std::max( 1.f, std::round( game.y * height ) ) );
        }

        // What the renderer puts on top of a game camera's own settings (Renderer.cpp, as it
        // renders a camera): the player's graphics settings and the hardware profile's caps.
        // Pixel-perfect cameras keep their own grid.
        struct RenderCaps
        {
            float PlayerScale = 1.f;
            Moonlight::UpscaleFilter Upscale = Moonlight::UpscaleFilter::Bilinear;
            float MaxScale = 0.f;
            uint64_t MaxPixels = 0;
            bool PlayerDynamic = false;
        };

        RenderCaps CapsFor( const Camera& InCamera )
        {
            RenderCaps caps;
            caps.Upscale = InCamera.Upscale;
            if( InCamera.PixelPerfect )
            {
                return caps;
            }
            const BGFXRenderer::FeatureCapSettings& feature = GetEngine().GetRenderer().FeatureCaps;
            caps.PlayerScale = std::clamp( feature.PlayerRenderScale, 0.1f, 1.f );
            if( feature.PlayerUpscale >= 0 && feature.PlayerUpscale <= static_cast<int>( Moonlight::UpscaleFilter::Sharp ) )
            {
                caps.Upscale = static_cast<Moonlight::UpscaleFilter>( feature.PlayerUpscale );
            }
            caps.MaxScale = feature.MaxRenderScale;
            caps.MaxPixels = feature.MaxRenderPixels;
            caps.PlayerDynamic = feature.PlayerDynamicResolution;
            return caps;
        }
    }

    void Register()
    {
        ReflectionUI::RegisterFieldDecorator( "Camera", "FieldOfView", { "FieldOfView" }, []( void* InInstance, const Reflection::TypeInfo& InType ) {
            Camera& camera = As<Camera>( InInstance );
            float* fov = FovOf( InInstance, InType );
            if( !fov || camera.Projection != Moonlight::ProjectionType::Perspective )
            {
                return false;
            }
            const char* note = "";
            const double sensor = SensorHeight( camera, note );
            double value = *fov;
            if( !MeWidgets::FocalLengthRow( "Focal Length", value, sensor, note ) )
            {
                return false;
            }
            *fov = static_cast<float>( value );
            return true;
        } );

        ReflectionUI::RegisterFieldDecorator( "Camera", "FieldOfView", { "FieldOfView" }, []( void* InInstance, const Reflection::TypeInfo& InType ) {
            Camera& camera = As<Camera>( InInstance );
            float* fov = FovOf( InInstance, InType );
            if( !fov || camera.Projection != Moonlight::ProjectionType::Perspective )
            {
                return false;
            }
            const char* note = "";
            const double sensor = SensorHeight( camera, note );
            double value = *fov;
            if( !MeWidgets::LensWedge( "Lens", value, sensor, note ) )
            {
                return false;
            }
            *fov = static_cast<float>( value );
            return true;
        } );

        ReflectionUI::RegisterFieldDecorator( "Camera", "RenderScale", { "RenderScale" }, []( void* InInstance, const Reflection::TypeInfo& ) {
            Camera& camera = As<Camera>( InInstance );
            const char* note = "";
            const Vector2 output = OutputSize( camera, note );
            const uint32_t outWidth = static_cast<uint32_t>( std::max( output.x, 1.f ) );
            const uint32_t outHeight = static_cast<uint32_t>( std::max( output.y, 1.f ) );
            const RenderCaps caps = CapsFor( camera );
            std::string why = note;
            if( camera.PixelPerfect )
            {
                why += "; Pixel Perfect overrides the scale";
            }
            else
            {
                if( caps.PlayerScale < 1.f )
                {
                    why += "; the player's render scale (" + std::to_string( static_cast<int>( std::lround( caps.PlayerScale * 100.f ) ) ) + "%) multiplies it";
                }
                if( caps.Upscale != camera.Upscale )
                {
                    why += "; the player's upscale filter replaces this camera's";
                }
                // Name the profile cap when it is what lowers the size at the current scale.
                const float scaled = camera.RenderScale * caps.PlayerScale;
                uint32_t freeW = 0, freeH = 0, freeF = 0, capW = 0, capH = 0, capF = 0;
                Moonlight::ComputeRenderSize( outWidth, outHeight, scaled, caps.Upscale, 0.f, 0, freeW, freeH, freeF );
                Moonlight::ComputeRenderSize( outWidth, outHeight, scaled, caps.Upscale, caps.MaxScale, caps.MaxPixels, capW, capH, capF );
                if( capW != freeW || capH != freeH || capF != freeF )
                {
                    why += "; capped by the hardware profile (";
                    if( caps.MaxScale > 0.f && scaled > caps.MaxScale )
                    {
                        why += "render scale at most " + std::to_string( static_cast<int>( std::lround( caps.MaxScale * 100.f ) ) ) + "%";
                    }
                    else
                    {
                        why += "at most " + std::to_string( caps.MaxPixels ) + " pixels";
                    }
                    why += ")";
                }
                if( camera.DynamicResolution || caps.PlayerDynamic )
                {
                    why += "; dynamic resolution lowers it from here when the GPU is slow";
                }
            }
            auto internal = [&camera, caps, outWidth, outHeight]( double InScale, int& OutWidth, int& OutHeight ) {
                uint32_t width = 0;
                uint32_t height = 0;
                uint32_t factor = 0;
                if( camera.PixelPerfect )
                {
                    camera.GetPixelPerfectSize( factor, width, height );
                }
                else
                {
                    Moonlight::ComputeRenderSize( outWidth, outHeight, static_cast<float>( InScale ) * caps.PlayerScale, caps.Upscale, caps.MaxScale, caps.MaxPixels, width, height, factor );
                }
                OutWidth = static_cast<int>( width );
                OutHeight = static_cast<int>( height );
            };
            double scale = camera.RenderScale;
            if( !MeWidgets::RenderScaleBar( "Resolution", scale, static_cast<int>( outWidth ), static_cast<int>( outHeight ), internal, why.c_str() ) )
            {
                return false;
            }
            camera.RenderScale = static_cast<float>( scale );
            return true;
        } );

        ReflectionUI::RegisterFieldDecorator( "PostProcess", "DoFAperture", { "DoFAperture" }, []( void* InInstance, const Reflection::TypeInfo& ) {
            PostProcess& post = As<PostProcess>( InInstance );
            double stop = post.DoFAperture;
            if( !MeWidgets::ApertureDial( "Iris", stop ) )
            {
                return false;
            }
            post.DoFAperture = static_cast<float>( stop );
            return true;
        } );

        ReflectionUI::RegisterFieldDecorator( "PostProcess", "DoFFocusDistance", { "DoFFocusDistance" }, []( void* InInstance, const Reflection::TypeInfo& ) {
            PostProcess& post = As<PostProcess>( InInstance );
            const char* note = "";
            const double focal = FocalLength( post, note );
            double focus = post.DoFFocusDistance;
            if( !MeWidgets::FocusBar( "Focus Band", focus, focal, std::clamp( post.DoFAperture, 0.7f, 32.f ), std::clamp( post.DoFSensorHeight, 1.f, 70.f ), note ) )
            {
                return false;
            }
            post.DoFFocusDistance = static_cast<float>( focus );
            return true;
        } );
    }
}

#endif
