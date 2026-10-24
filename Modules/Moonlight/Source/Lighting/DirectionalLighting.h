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
    // A procedural sky owns every directional's direction and radiance, except Manual lights,
    // which keep their authored transform, colour and intensity alongside it. Authored lights
    // supply tint, intensity and shadow controls; without a procedural sky their transforms apply.
    inline DirectionalLighting ResolveDirectionalLighting( const std::vector<LightCommand>& InLights, const SkyDirectionalLights* InSky )
    {
        DirectionalLighting result;
        size_t skyDriven = 0;
        for( const LightCommand& light : InLights )
        {
            if( light.Type == LightType::Directional && result.Count < result.Lights.size() )
            {
                if( light.Source != DirectionalSource::Manual ) ++skyDriven;
                result.Lights[result.Count] = light;
                result.Lights[result.Count++].SkySun = false;
            }
        }
        // Manual lights don't count here: a sky with no sky-driven directional still lights the scene.
        if( skyDriven == 0 && InSky && result.Count < result.Lights.size() )
        {
            LightCommand& automatic = result.Lights[result.Count++];
            automatic.Type = LightType::Directional;
            automatic.CastShadows = true;
        }
        if( InSky )
        {
            size_t automatic = result.Count;
            bool explicitMoon = false;
            for( size_t i = 0; i < result.Count; ++i )
            {
                const LightCommand& light = result.Lights[i];
                if( light.Source == DirectionalSource::Moon ) explicitMoon = true;
                else if( light.Source != DirectionalSource::Sun && light.Source != DirectionalSource::Manual &&
                    ( automatic == result.Count || ( !result.Lights[automatic].CastShadows && light.CastShadows ) ) )
                {
                    automatic = i;
                }
            }
            // Automatic (including legacy Transform sources) retains both bodies at dusk.
            // Reserve a lunar slot even at the uniform limit so night never loses its light.
            if( automatic < result.Count && !explicitMoon )
            {
                LightCommand moon = result.Lights[automatic];
                moon.Source = DirectionalSource::Moon;
                if( result.Count < result.Lights.size() ) result.Lights[result.Count++] = moon;
                else
                {
                    // At the limit, replace the last sky-driven light — never a Manual light — and
                    // keep the automatic sun before its moon so dusk retains solar shadows.
                    size_t evict = result.Count;
                    for( size_t i = result.Count; i-- > 0; )
                    {
                        if( i != automatic && result.Lights[i].Source != DirectionalSource::Manual )
                        {
                            evict = i;
                            break;
                        }
                    }
                    if( evict < automatic )
                    {
                        result.Lights[evict] = result.Lights[automatic];
                        result.Lights[automatic] = moon;
                    }
                    else if( evict < result.Count )
                    {
                        result.Lights[evict] = moon;
                    }
                }
            }
            for( size_t i = 0; i < result.Count; ++i )
            {
                LightCommand& light = result.Lights[i];
                if( light.Source == DirectionalSource::Manual ) continue; // the authored transform and colour apply
                const bool moon = light.Source == DirectionalSource::Moon;
                const CelestialLight& body = moon ? InSky->Moon : InSky->Sun;
                light.Source = moon ? DirectionalSource::Moon : DirectionalSource::Sun;
                light.Direction = body.Direction * -1.f;
                light.Color = Vector3( light.Color.x * body.Color.x, light.Color.y * body.Color.y, light.Color.z * body.Color.z );
                light.SkySun = !moon;
            }
        }
        // Keep solar light during eclipses for receivers outside the camera's umbra.
        // Only one CSM atlas is available. Prefer a visible caster, including the moon at night.
        auto luminance = []( const Vector3& c ) { return c.x * 0.2126f + c.y * 0.7152f + c.z * 0.0722f; };
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
