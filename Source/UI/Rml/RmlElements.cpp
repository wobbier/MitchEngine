#include "PCH.h"
#include "UI/Rml/RmlElements.h"

#if USING( ME_RMLUI )

#include "Engine/Engine.h"
#include "Engine/Input.h"
#include "Graphics/SpriteSheet.h"
#include "Input/InputActions.h"
#include "Resource/ResourceCache.h"
#include "VFS/VFS.h"
#include <RmlUi/Core.h>
#include <RmlUi/Core/ElementInstancer.h>
#include <RmlUi/Core/Geometry.h>
#include <RmlUi/Core/Mesh.h>
#include <RmlUi/Core/MeshUtilities.h>
#include <RmlUi/Core/RenderManager.h>
#include <RmlUi/Core/Texture.h>
#include <cmath>

namespace
{
    // An image from an engine source: a texture and the part of it shown.
    class EngineImageElement
        : public Rml::Element
    {
    public:
        explicit EngineImageElement( const Rml::String& InTag )
            : Rml::Element( InTag )
        {
        }

    protected:
        bool GetIntrinsicDimensions( Rml::Vector2f& OutDimensions, float& OutRatio ) override
        {
            Resolve();
            OutDimensions = m_size;
            OutRatio = m_size.y > 0.f ? m_size.x / m_size.y : 1.f;
            return m_size.x > 0.f && m_size.y > 0.f;
        }

        void OnAttributeChange( const Rml::ElementAttributes& InChanged ) override
        {
            Rml::Element::OnAttributeChange( InChanged );
            m_resolved = false;
            m_geometry = {};
            DirtyLayout();
        }

        void OnResize() override
        {
            m_geometry = {};
        }

        void OnUpdate() override
        {
            Resolve();
            // Flipbooks advance on the UI clock (simulated time, so captures stay repeatable).
            if( !m_frames.empty() )
            {
                const double time = Rml::GetSystemInterface()->GetElapsedTime();
                const size_t frame = static_cast<size_t>( std::floor( time * m_fps ) ) % m_frames.size();
                if( frame != m_frame )
                {
                    m_frame = frame;
                    SetRegion( m_frames[frame] );
                }
            }
        }

        void OnRender() override
        {
            Resolve();
            if( !m_texture )
            {
                return;
            }
            if( !m_geometry )
            {
                const Rml::Vector2f size = GetBox().GetSize( Rml::BoxArea::Content );
                const float opacity = GetComputedValues().opacity();
                Rml::Mesh mesh;
                Rml::MeshUtilities::GenerateQuad( mesh, Rml::Vector2f( 0.f ), size, Rml::ColourbPremultiplied( static_cast<Rml::byte>( 255 * opacity ), static_cast<Rml::byte>( 255 * opacity ) ),
                    Rml::Vector2f( m_uv.x, m_uv.y ), Rml::Vector2f( m_uv.z, m_uv.w ) );
                m_geometry = GetRenderManager()->MakeGeometry( std::move( mesh ) );
            }
            m_geometry.Render( GetAbsoluteOffset( Rml::BoxArea::Content ), m_texture );
        }

        // The texture and the region (texels: x, y, w, h; zero size = all of it).
        void SetSource( const std::string& InTexture, const Rml::Vector4f& InRegion )
        {
            if( InTexture != m_source )
            {
                m_source = InTexture;
                m_texture = InTexture.empty() ? Rml::Texture() : GetRenderManager()->LoadTexture( InTexture );
            }
            SetRegion( InRegion );
        }

        void SetRegion( const Rml::Vector4f& InRegion )
        {
            const Rml::Vector2i dimensions = m_texture ? m_texture.GetDimensions() : Rml::Vector2i( 1, 1 );
            const Rml::Vector2f texture( static_cast<float>( std::max( dimensions.x, 1 ) ), static_cast<float>( std::max( dimensions.y, 1 ) ) );
            const bool region = InRegion.z > 0.f && InRegion.w > 0.f;
            m_uv = region ? Rml::Vector4f( InRegion.x / texture.x, InRegion.y / texture.y, ( InRegion.x + InRegion.z ) / texture.x, ( InRegion.y + InRegion.w ) / texture.y ) : Rml::Vector4f( 0.f, 0.f, 1.f, 1.f );
            const Rml::Vector2f size = region ? Rml::Vector2f( InRegion.z, InRegion.w ) : texture;
            if( size != m_size )
            {
                m_size = size;
                DirtyLayout();
            }
            m_geometry = {};
        }

        virtual void Load() = 0;

        std::vector<Rml::Vector4f> m_frames;
        float m_fps = 10.f;

    private:
        void Resolve()
        {
            if( !m_resolved )
            {
                m_resolved = true;
                m_frames.clear();
                m_frame = static_cast<size_t>( -1 );
                Load();
            }
        }

        bool m_resolved = false;
        std::string m_source;
        Rml::Texture m_texture;
        Rml::Geometry m_geometry;
        Rml::Vector4f m_uv = Rml::Vector4f( 0.f, 0.f, 1.f, 1.f );
        Rml::Vector2f m_size = Rml::Vector2f( 0.f );
        size_t m_frame = static_cast<size_t>( -1 );
    };


    class SpriteElement final
        : public EngineImageElement
    {
    public:
        using EngineImageElement::EngineImageElement;

    protected:
        void Load() override
        {
            const std::string anim = GetAttribute<Rml::String>( "anim", "" );
            std::string sheetPath = GetAttribute<Rml::String>( "sheet", "" );
            std::vector<std::string> names;
            if( !anim.empty() )
            {
                std::string text;
                Moonlight::SpriteAnimation animation;
                if( VFS::Get().ReadText( Path( anim ), text ) && animation.Parse( text ) )
                {
                    sheetPath = animation.Sheet;
                    names = animation.Frames;
                    m_fps = GetAttribute<float>( "fps", animation.FramesPerSecond );
                }
                else
                {
                    YIKES( "[UI] <sprite>: can't read the flipbook " + anim );
                }
            }
            else
            {
                names.push_back( GetAttribute<Rml::String>( "name", "" ) );
            }
            const Path sheetFile( sheetPath );
            SharedPtr<Moonlight::SpriteSheet> sheet = !sheetPath.empty() && sheetFile.Exists ? ResourceCache::GetInstance().Get<Moonlight::SpriteSheet>( sheetFile ) : nullptr;
            if( !sheet )
            {
                YIKES( "[UI] <sprite>: no sprite sheet " + sheetPath );
                return;
            }
            for( const std::string& name : names )
            {
                const Moonlight::SpriteFrame* frame = sheet->Find( name );
                const Rml::Vector4f region = frame ? Rml::Vector4f( frame->Rect.x, frame->Rect.y, frame->Rect.z, frame->Rect.w ) : Rml::Vector4f( 0.f );
                m_frames.push_back( region );
            }
            SetSource( sheet->Texture, m_frames.empty() ? Rml::Vector4f( 0.f ) : m_frames.front() );
            if( anim.empty() )
            {
                m_frames.clear();   // a still sprite: nothing to advance
            }
        }
    };


    class ViewportElement final
        : public EngineImageElement
    {
    public:
        using EngineImageElement::EngineImageElement;

    protected:
        void Load() override
        {
            // RmlRenderInterface::LoadTexture hands back the render texture's own (live) image.
            SetSource( GetAttribute<Rml::String>( "src", "" ), Rml::Vector4f( 0.f ) );
        }
    };


    class InputGlyphElement final
        : public Rml::Element
    {
    public:
        explicit InputGlyphElement( const Rml::String& InTag )
            : Rml::Element( InTag )
        {
        }

    protected:
        void OnUpdate() override
        {
            const std::string action = GetAttribute<Rml::String>( "action", "" );
            const size_t index = static_cast<size_t>( std::max( 0, GetAttribute<int>( "index", 0 ) ) );
            std::string text = action.empty() ? std::string() : GetEngine().GetInput().GetActions().GetBindingDisplay( action, index );
            if( text.empty() )
            {
                text = "?";
            }
            if( text != m_text )
            {
                m_text = text;
                // Key names are data, not prose to translate.
                std::string escaped;
                for( const char c : text )
                {
                    escaped += c == '<' ? std::string( "&lt;" ) : c == '>' ? std::string( "&gt;" ) : c == '&' ? std::string( "&amp;" ) : std::string( 1, c );
                }
                SetInnerRML( escaped );
            }
        }

    private:
        std::string m_text;
    };
}


void RmlElements::Register()
{
    static Rml::ElementInstancerGeneric<SpriteElement> s_sprite;
    static Rml::ElementInstancerGeneric<ViewportElement> s_viewport;
    static Rml::ElementInstancerGeneric<InputGlyphElement> s_glyph;
    Rml::Factory::RegisterElementInstancer( "sprite", &s_sprite );
    Rml::Factory::RegisterElementInstancer( "viewport", &s_viewport );
    Rml::Factory::RegisterElementInstancer( "input-glyph", &s_glyph );
}

#endif
