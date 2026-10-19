#pragma once
#include "Camera/ProceduralSkySettings.h"
#include "Graphics/ShaderCommand.h"
#include "Lighting/DirectionalLighting.h"
#include "bgfx/bgfx.h"
#include "bx/math.h"
#include <cstdint>
#include <thread>
#include <vector>

namespace Moonlight
{
    struct CameraData;
    class ViewAllocator;

    // Where the sky's bodies are for a date, time and latitude (world: +Y up, +Z north, +X east).
    struct SkyCelestial
    {
        bx::Vec3 Sun = { 0.f, 1.f, 0.f };           // towards the sun
        bx::Vec3 Moon = { 0.f, -1.f, 0.f };         // towards the moon
        bx::Vec3 MoonUp = { 0.f, 0.f, 1.f };        // celestial north across the moon's disc
        bx::Vec3 Equatorial[3] = { { 1.f, 0.f, 0.f }, { 0.f, 1.f, 0.f }, { 0.f, 0.f, 1.f } };   // the equatorial frame's axes in the world
        bx::Vec3 GalaxyPole = { 0.f, 1.f, 0.f };
        bx::Vec3 GalaxyCentre = { 1.f, 0.f, 0.f };
        float MoonLitFraction = 0.f;                // of the disc we see
        float MoonBrightness = 0.f;                 // Lambert phase law: 1 full, 1/pi at the quarters, 0 new
    };
    SkyCelestial ComputeSkyCelestial( const ProceduralSkySettings& InSettings );

    // Uncovered fraction of a solar disc: handles disjoint, partial, annular and total eclipses.
    float ComputeDiscVisibility( float InSunRadius, float InMoonRadius, float InSeparation );
    struct SolarEclipse
    {
        bx::Vec3 Sun = { 0.f, 1.f, 0.f };
        bx::Vec3 Moon = { 0.f, -1.f, 0.f };
        float SunRadius = 0.004654f;
        float MoonRadius = 0.004520f;
        // World metres. Parallax projects the lunar umbra and penumbra onto scene receivers.
        static constexpr float MoonDistance = 384400000.f;
        float Visibility( const bx::Vec3& InWorldPosition ) const;
        void Uniforms( float OutSun[4], float OutMoon[4], float OutParams[4] ) const;
    };

    // Fraction of a light disc's light (angular radius InRadius, zenith cosine InMu) that reaches
    // InAltitudeKm through the atmosphere (aerosol density InAerosol), zero below the horizon.
    bx::Vec3 ComputeSkyTransmittance( float InAltitudeKm, float InMu, float InAerosol, float InRadius );

    // The procedural sky (Camera background "Procedural Sky"):
    //   - a physically based atmosphere (Rayleigh, Mie and ozone; Hillaire 2020 lookup tables), its
    //     haze set by the turbidity;
    //   - the sun and a phase-lit moon placed by the date, time of day and latitude, and a star field
    //     with the Milky Way turning with the sidereal time;
    //   - raymarched volumetric clouds (Schneider 2015 style noise, generated at start-up on worker
    //     threads) accumulated over frames at half resolution.
    // World directions: +Y up, +Z north, +X east. Every directional light follows these
    // bodies for procedural skies; scenes without one get automatic shadow-casting lights.
    class DynamicSky
    {
    public:
        DynamicSky();
        ~DynamicSky();

        // Per camera, before anything uses the sky this frame: places the sun, moon and stars for the
        // settings and the camera, and works out the light they cast. InTimeSeconds moves the clouds.
        void ApplySettings( const ProceduralSkySettings& InSettings, const CameraData& InCamera, float InTimeSeconds );

        // The camera's sky passes (lookup tables, clouds); before its scene view and environment probe.
        void Prepare( ViewAllocator& InViews, CameraData& InCamera );

        // Draws the sky behind everything already in the depth buffer (submit after the opaque scene).
        void Draw( bgfx::ViewId InView, const CameraData& InCamera );

        // Binds the sky for an environment probe face and returns the program to submit.
        const ShaderCommand& BindCapture();

        const SkyDirectionalLights& GetDirectionalLights() const { return m_directionalLights; }
        const SolarEclipse& GetEclipse() const { return m_eclipse; }
        // Sky ambient for the hemisphere fallback (used until an environment probe is ready).
        const bx::Vec3& GetAmbientSky() const { return m_ambientSky; }
        // Changes whenever the sky looks different enough to recapture an environment probe.
        uint64_t GetEnvironmentKey() const { return m_environmentKey; }


    private:
        struct Noise
        {
            std::vector<uint8_t> Shape;     // kShapeSize^3, R8
            std::vector<uint8_t> Detail;    // kDetailSize^3, R8
            std::vector<uint8_t> Weather;   // kWeatherSize^2, RG8: coverage, cloud type
        };
        static void GenerateNoise( Noise& OutNoise );

        bool EnsureNoise();
        bool CloudsEnabled() const;
        float CloudVisibility( const bx::Vec3& InLightDir ) const;
        float CloudDensity( float InX, float InAltitude, float InZ ) const;
        void UpdateSceneLight();
        void SetCommonUniforms( bool InClouds, float InSteps, float InLightSteps );
        void BindTextures( bool InClouds );
        void RenderLuts( ViewAllocator& InViews );
        void RenderClouds( ViewAllocator& InViews, CameraData& InCamera );
        void ComputeBasis( const CameraData& InCamera, float OutRight[4], float OutUp[4], float OutForward[4], float& OutTanX, float& OutTanY ) const;

        // Programs (hot reloadable)
        ShaderCommand m_skyProgram;
        ShaderCommand m_transmittanceProgram;
        ShaderCommand m_multiScatterProgram;
        ShaderCommand m_skyViewProgram;
        ShaderCommand m_cloudProgram;
        ShaderCommand m_captureProgram;

        // Lookup tables
        bgfx::TextureHandle m_transmittanceLut = BGFX_INVALID_HANDLE;
        bgfx::TextureHandle m_multiScatterLut = BGFX_INVALID_HANDLE;
        bgfx::TextureHandle m_skyViewLut = BGFX_INVALID_HANDLE;
        bgfx::FrameBufferHandle m_transmittanceBuffer = BGFX_INVALID_HANDLE;
        bgfx::FrameBufferHandle m_multiScatterBuffer = BGFX_INVALID_HANDLE;
        bgfx::FrameBufferHandle m_skyViewBuffer = BGFX_INVALID_HANDLE;
        float m_lutAerosol = -1.f;          // what the transmittance/multiple scattering tables hold

        // Cloud noise: generated on a worker thread, uploaded on first use.
        std::thread m_noiseThread;
        Noise m_noise;
        bool m_noiseReady = false;
        bgfx::TextureHandle m_shapeTexture = BGFX_INVALID_HANDLE;
        bgfx::TextureHandle m_detailTexture = BGFX_INVALID_HANDLE;
        bgfx::TextureHandle m_weatherTexture = BGFX_INVALID_HANDLE;
        bgfx::TextureHandle m_blackTexture = BGFX_INVALID_HANDLE;
        bgfx::TextureHandle m_blackVolume = BGFX_INVALID_HANDLE;

        // Uniforms
        bgfx::UniformHandle u_skySun = BGFX_INVALID_HANDLE;
        bgfx::UniformHandle u_skySunColor = BGFX_INVALID_HANDLE;
        bgfx::UniformHandle u_skyMoon = BGFX_INVALID_HANDLE;
        bgfx::UniformHandle u_skyMoonColor = BGFX_INVALID_HANDLE;
        bgfx::UniformHandle u_skyAtmosphere = BGFX_INVALID_HANDLE;
        bgfx::UniformHandle u_skyCamera = BGFX_INVALID_HANDLE;
        bgfx::UniformHandle u_skyScattering = BGFX_INVALID_HANDLE;
        bgfx::UniformHandle u_skyBasisRight = BGFX_INVALID_HANDLE;
        bgfx::UniformHandle u_skyBasisUp = BGFX_INVALID_HANDLE;
        bgfx::UniformHandle u_skyBasisForward = BGFX_INVALID_HANDLE;
        bgfx::UniformHandle u_skyStarsX = BGFX_INVALID_HANDLE;
        bgfx::UniformHandle u_skyStarsY = BGFX_INVALID_HANDLE;
        bgfx::UniformHandle u_skyStarsZ = BGFX_INVALID_HANDLE;
        bgfx::UniformHandle u_skyGalaxy = BGFX_INVALID_HANDLE;
        bgfx::UniformHandle u_skyGalaxyCentre = BGFX_INVALID_HANDLE;
        bgfx::UniformHandle u_skyMoonUp = BGFX_INVALID_HANDLE;
        bgfx::UniformHandle u_eclipseSun = BGFX_INVALID_HANDLE;
        bgfx::UniformHandle u_eclipseMoon = BGFX_INVALID_HANDLE;
        bgfx::UniformHandle u_eclipseParams = BGFX_INVALID_HANDLE;
        bgfx::UniformHandle u_cloudLayer = BGFX_INVALID_HANDLE;
        bgfx::UniformHandle u_cloudWind = BGFX_INVALID_HANDLE;
        bgfx::UniformHandle u_cloudMarch = BGFX_INVALID_HANDLE;
        bgfx::UniformHandle u_cloudHistoryRight = BGFX_INVALID_HANDLE;
        bgfx::UniformHandle u_cloudHistoryUp = BGFX_INVALID_HANDLE;
        bgfx::UniformHandle u_cloudHistoryForward = BGFX_INVALID_HANDLE;
        bgfx::UniformHandle u_cloudFrame = BGFX_INVALID_HANDLE;
        bgfx::UniformHandle s_skyTransmittance = BGFX_INVALID_HANDLE;
        bgfx::UniformHandle s_skyMultiScatter = BGFX_INVALID_HANDLE;
        bgfx::UniformHandle s_skyView = BGFX_INVALID_HANDLE;
        bgfx::UniformHandle s_skyClouds = BGFX_INVALID_HANDLE;
        bgfx::UniformHandle s_cloudHistory = BGFX_INVALID_HANDLE;
        bgfx::UniformHandle s_cloudShape = BGFX_INVALID_HANDLE;
        bgfx::UniformHandle s_cloudDetail = BGFX_INVALID_HANDLE;
        bgfx::UniformHandle s_cloudWeather = BGFX_INVALID_HANDLE;

        // This camera's sky (ApplySettings)
        ProceduralSkySettings m_settings;
        float m_timeSeconds = 0.f;
        bx::Vec3 m_cameraKm = { 0.f, 0.01f, 0.f };   // x, altitude, z
        SkyCelestial m_bodies;
        bx::Vec3 m_sunIlluminance = { 0.f, 0.f, 0.f };
        bx::Vec3 m_moonIlluminance = { 0.f, 0.f, 0.f };
        float m_aerosol = 1.f;
        float m_windOffset[3] = { 0.f, 0.f, 0.f };
        bool m_showsSky = false;                    // the camera's background is this sky
        SkyDirectionalLights m_directionalLights;
        SolarEclipse m_eclipse;
        bx::Vec3 m_ambientSky = { 0.f, 0.f, 0.f };
        uint64_t m_environmentKey = 0;
    };
}
