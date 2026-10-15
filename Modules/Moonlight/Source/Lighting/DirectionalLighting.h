#pragma once
#include "RenderCommands.h"
#include <algorithm>
#include <array>

namespace Moonlight
{
    struct CelestialLight
    {
        Vector3 Direction = Vector3( 0.f, 1.f, 0.f ); // towards the body
        Vector3 Color;                               // linear illuminance, before eclipse attenuation
    };

    struct SkyDirectionalLights
    {
        CelestialLight Sun;
        CelestialLight Moon;
        float SunVisibility = 1.f;
    };

    struct DirectionalLighting
    {
        std::array<LightCommand, 4> Lights;
        size_t Count = 0;
    };

    // Resolve fresh copies for every camera: no sky state can leak into another camera or the ECS.
    // The primary authored directional supplies tint, intensity and shadow controls to the sky.
    inline DirectionalLighting ResolveDirectionalLighting( const std::vector<LightCommand>& InLights, const SkyDirectionalLights* InSky )
    {
        DirectionalLighting result;
        for( const LightCommand& light : InLights )
        {
            if( light.Type == LightType::Directional && result.Count < result.Lights.size() )
                result.Lights[result.Count++] = light;
        }
        if( result.Count == 0 && InSky )
        {
            result.Count = 1;
            result.Lights[0].Type = LightType::Directional;
            result.Lights[0].CastShadows = true;
        }
        size_t primary = 0;
        for( size_t i = 0; i < result.Count; ++i )
        {
            if( result.Lights[i].CastShadows ) { primary = i; break; }
        }
        auto luminance = []( const Vector3& c ) { return c.x * 0.2126f + c.y * 0.7152f + c.z * 0.0722f; };
        for( size_t i = 0; i < result.Count; ++i )
        {
            LightCommand& light = result.Lights[i];
            light.SkySun = false;
            if( !InSky || light.Source == DirectionalSource::Transform ||
                ( light.Source == DirectionalSource::Automatic && i != primary ) ) continue;
            // Keep the sun available during an eclipse: receivers outside the umbra can still
            // see it even when the camera is in totality. Eclipse attenuation is per receiver.
            const bool moon = light.Source == DirectionalSource::Moon ||
                ( light.Source == DirectionalSource::Automatic && luminance( InSky->Moon.Color ) > luminance( InSky->Sun.Color ) );
            const CelestialLight& body = moon ? InSky->Moon : InSky->Sun;
            light.Direction = body.Direction * -1.f;
            light.Color = Vector3( light.Color.x * body.Color.x, light.Color.y * body.Color.y, light.Color.z * body.Color.z );
            light.SkySun = !moon;
        }
        // Only one CSM atlas is available. Prefer a visible caster, including the moon at night.
        for( size_t i = 0; i < result.Count; ++i )
        {
            const LightCommand& light = result.Lights[i];
            if( light.CastShadows && luminance( light.Color ) * ( light.SkySun ? InSky->SunVisibility : 1.f ) > 1e-6f )
            {
                std::swap( result.Lights[0], result.Lights[i] );
                break;
            }
        }
        return result;
    }
}
