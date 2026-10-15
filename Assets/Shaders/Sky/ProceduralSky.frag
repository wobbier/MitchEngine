$input v_skyDir

// The sky behind the scene: the atmosphere (sky-view table) and the night glow; beyond it the stars
// and the Milky Way turning with the sidereal time, the moon (a sphere lit by the sun, so its phase
// follows from where the sun is) and the sun's disc, all dimmed by the air in front of them; and
// the clouds (Clouds.frag, accumulated at half resolution) over everything.
#include "../Common.sh"
#include "Atmosphere.sh"

uniform vec4 u_skyStarsX;       // xyz world -> celestial frame (row 0), w star brightness
uniform vec4 u_skyStarsY;       // xyz row 1, w twinkle clock (seconds)
uniform vec4 u_skyStarsZ;       // xyz row 2, w angle a pixel covers (radians)
uniform vec4 u_skyGalaxy;       // xyz galactic north pole (world), w Milky Way brightness
uniform vec4 u_skyGalaxyCentre; // xyz galactic centre (world)
uniform vec4 u_skyMoonUp;       // xyz celestial north across the moon's disc, w earthshine

uniform vec4 u_skyBasisRight;
uniform vec4 u_skyBasisUp;
uniform vec4 u_skyBasisForward;

SAMPLER2D(s_skyClouds, 3);
SAMPLER3D(s_cloudShape, 4);
SAMPLER3D(s_cloudDetail, 5);

// Hash without sines (Dave Hoskins): three values in 0..1 from a small-integer cell.
vec3 skyHash(vec3 _p)
{
	_p = fract(_p * vec3(0.1031, 0.1030, 0.0973));
	_p += dot(_p, _p.yxz + 33.33);
	return fract((_p.xxy + _p.yxx) * _p.zyx);
}

// A thin spherical shell of 3D cells avoids cube-face seams and stretched star shapes.
// Evaluate each Gaussian in pixel coordinates using the local direction derivatives: stars keep
// the same round footprint in wide, tall and off-axis views, and stay stable as the camera turns.
vec3 skyStars(vec3 _celestial, vec3 _dx, vec3 _dy, float _time)
{
    const float cells = 128.0;
    vec3 cell = floor(_celestial * cells);
    float xx = dot(_dx, _dx);
    float yy = dot(_dy, _dy);
    float xy = dot(_dx, _dy);
    float determinant = max(xx * yy - xy * xy, 1e-20);
    vec3 light = vec3_splat(0.0);
    for (int z = -1; z <= 1; ++z)
    {
        for (int y = -1; y <= 1; ++y)
        {
            for (int x = -1; x <= 1; ++x)
            {
                vec3 c = cell + vec3(float(x), float(y), float(z));
                vec3 h = skyHash(c + 11.0);
                if (h.z < 0.94) continue;
                vec3 starPosition = c + 0.1 + 0.8 * skyHash(c * 1.37 + 7.0);
                float distance = length(starPosition);
                if (abs(distance - cells) > 0.5) continue;
                vec3 offset = starPosition / distance - _celestial;
                float ox = dot(offset, _dx);
                float oy = dot(offset, _dy);
                vec2 pixel = vec2(ox * yy - oy * xy, oy * xx - ox * xy) / determinant;
                float shape = exp(-dot(pixel, pixel) / (2.0 * 0.55 * 0.55));
                float flux = min(0.012 * pow(max((1.0 - h.z) / 0.06, 1e-5), -0.65), 1.2);
                float temperature = fract(h.x * 13.7 + h.y * 7.3);
                vec3 tint = mix(vec3(0.62, 0.74, 1.0), vec3(1.0, 0.97, 0.94), smoothstep(0.0, 0.25, temperature));
                tint = mix(tint, vec3(1.0, 0.86, 0.66), smoothstep(0.55, 0.8, temperature));
                tint = mix(tint, vec3(1.0, 0.66, 0.45), smoothstep(0.88, 1.0, temperature));
                float twinkle = 1.0 + 0.12 * sin(_time * (2.0 + 5.0 * h.x) + h.y * 40.0);
                light += tint * (flux * twinkle * shape);
            }
        }
    }
    return light * 6.0;
}

// Fixed lunar surface noise, independent of the animated cloud field and its texture support.
float skyMoonNoise(vec3 _p)
{
    vec3 cell = floor(_p);
    vec3 f = fract(_p);
    f = f * f * (3.0 - 2.0 * f);
    float a = mix(skyHash(cell).x, skyHash(cell + vec3(1.0, 0.0, 0.0)).x, f.x);
    float b = mix(skyHash(cell + vec3(0.0, 1.0, 0.0)).x, skyHash(cell + vec3(1.0, 1.0, 0.0)).x, f.x);
    float c = mix(skyHash(cell + vec3(0.0, 0.0, 1.0)).x, skyHash(cell + vec3(1.0, 0.0, 1.0)).x, f.x);
    float d = mix(skyHash(cell + vec3(0.0, 1.0, 1.0)).x, skyHash(cell + vec3(1.0, 1.0, 1.0)).x, f.x);
    return mix(mix(a, b, f.y), mix(c, d, f.y), f.z);
}

// A circular celestial billboard in the camera plane. Project the centre once, then use one
// scale for both axes: perspective at the edges of an ultrawide view mustn't squash the moon.
bool skyDiscCoordinates(vec3 _dir, vec3 _body, float _radius, out vec2 _disc)
{
    vec3 right = normalize(u_skyBasisRight.xyz);
    vec3 up = normalize(u_skyBasisUp.xyz);
    vec3 forward = u_skyBasisForward.xyz;
    vec3 view = vec3(dot(_dir, right), dot(_dir, up), dot(_dir, forward));
    vec3 body = vec3(dot(_body, right), dot(_body, up), dot(_body, forward));
    _disc = vec2_splat(0.0);
    if (body.z <= 0.0 || view.z <= 0.0) return false;
    _disc = (view.xy / view.z - body.xy / body.z) * body.z / tan(_radius);
    return true;
}

// A soft band along the galactic plane, brightest towards the galactic centre, mottled by star clouds
// and split by dark dust lanes.
vec3 skyMilkyWay(vec3 _dir, vec3 _celestial)
{
	float latitude = dot(_dir, u_skyGalaxy.xyz);
	float band = exp(-latitude * latitude * 18.0);
	if (band < 1e-3)
	{
		return vec3_splat(0.0);
	}
	float bulge = pow(saturate(dot(_dir, u_skyGalaxyCentre.xyz) * 0.5 + 0.5), 5.0);
	float starClouds = skyTexture3DLod(s_cloudShape, _celestial * 0.45 + vec3(0.17, 0.41, 0.73), 0.0).r;
	float dustNoise = skyTexture3DLod(s_cloudDetail, _celestial * 1.7 + vec3(0.5, 0.2, 0.9), 0.0).r;
	float dust = exp(-latitude * latitude * 400.0) * smoothstep(0.35, 0.75, dustNoise);
	float glow = band * (0.35 + 2.0 * bulge) * mix(0.25, 1.4, starClouds) * (1.0 - 0.8 * dust);
	return u_skyGalaxy.w * glow * mix(vec3(0.78, 0.84, 1.0), vec3(1.0, 0.9, 0.75), bulge);
}

// One octave of craters on the moon's unit sphere: in each cell of a 3D grid, maybe a bowl with a
// raised rim. xyz the height's gradient, w the height (both in units of the moon's radius).
vec4 skyMoonCraters(vec3 _p, float _cells, float _seed)
{
	vec3 cell = floor(_p * _cells);
	vec4 result = vec4_splat(0.0);
	for (int z = -1; z <= 1; ++z)
	{
		for (int y = -1; y <= 1; ++y)
		{
			for (int x = -1; x <= 1; ++x)
			{
				vec3 c = cell + vec3(float(x), float(y), float(z));
				vec3 h = skyHash(c + _seed);
				if (h.z > 0.6)
				{
					continue;
				}
				vec3 centre = (c + 0.2 + 0.6 * skyHash(c * 1.37 + _seed + 7.0)) / _cells;
				float radius = (0.15 + 0.4 * h.x * h.x) / _cells;
				vec3 delta = _p - centre;
				float distance = length(delta);
				float d = distance / radius;
				if (d >= 1.8)
				{
					continue;
				}
				// A bowl below the ground around it and a rim that falls away outside.
				float rimShape = exp(-(d - 1.0) * (d - 1.0) * 14.0);
				float height = (d < 1.0 ? d * d - 1.0 : 0.0) + 0.45 * rimShape;
				float slope = (d < 1.0 ? 2.0 * d : 0.0) - 12.6 * (d - 1.0) * rimShape;
				float depth = radius * 0.5;
				result += vec4(slope * depth / radius * delta / max(distance, 1e-6), height * depth);
			}
		}
	}
	return result;
}

// A mare: a dark basin of angular radius _radius around _centre, its shore roughened by _edge.
float skyMare(vec3 _p, vec3 _centre, float _radius, float _edge)
{
	float angle = acos(clamp(dot(_p, _centre), -1.0, 1.0));
	return 1.0 - smoothstep(_radius * 0.75, _radius * 1.05, angle + _edge * _radius);
}

// The near side's maria, so the moon is recognisably the Moon (selenographic positions: x east,
// y north, z towards the Earth).
float skyMaria(vec3 _p, float _edge)
{
	float mare = 0.0;
	mare = max(mare, skyMare(_p, vec3(-0.7976, 0.3090, 0.5180), 0.3840, _edge));   // Oceanus Procellarum
	mare = max(mare, skyMare(_p, vec3(-0.7869, 0.0523, 0.6148), 0.2793, _edge));
	mare = max(mare, skyMare(_p, vec3(-0.6683, 0.5299, 0.5221), 0.2269, _edge));
	mare = max(mare, skyMare(_p, vec3(-0.2312, 0.5446, 0.8062), 0.3142, _edge));   // Imbrium
	mare = max(mare, skyMare(_p, vec3(0.2655, 0.4695, 0.8421), 0.1833, _edge));    // Serenitatis
	mare = max(mare, skyMare(_p, vec3(0.5094, 0.1478, 0.8478), 0.2094, _edge));    // Tranquillitatis
	mare = max(mare, skyMare(_p, vec3(0.8197, 0.2924, 0.4925), 0.1484, _edge));    // Crisium
	mare = max(mare, skyMare(_p, vec3(0.7700, -0.1357, 0.6235), 0.1745, _edge));   // Fecunditatis
	mare = max(mare, skyMare(_p, vec3(0.7189, -0.0349, 0.6942), 0.1222, _edge));
	mare = max(mare, skyMare(_p, vec3(0.5540, -0.2588, 0.7912), 0.0960, _edge));   // Nectaris
	mare = max(mare, skyMare(_p, vec3(-0.2667, -0.3584, 0.8947), 0.1920, _edge));  // Nubium
	mare = max(mare, skyMare(_p, vec3(-0.0971, 0.8290, 0.5507), 0.1047, _edge));   // Frigoris
	mare = max(mare, skyMare(_p, vec3(0.1410, 0.8387, 0.5261), 0.1047, _edge));
	mare = max(mare, skyMare(_p, vec3(-0.3290, 0.8192, 0.4698), 0.0873, _edge));
	mare = max(mare, skyMare(_p, vec3(-0.5749, -0.4067, 0.7100), 0.1134, _edge));  // Humorum
	mare = max(mare, skyMare(_p, vec3(0.0612, 0.2250, 0.9724), 0.0698, _edge));    // Vaporum
	mare = max(mare, skyMare(_p, vec3(-0.5091, 0.1305, 0.8507), 0.1396, _edge));   // Insularum
	mare = max(mare, skyMare(_p, vec3(-0.3848, -0.1736, 0.9065), 0.1047, _edge));  // Cognitum
	return mare;
}

// A young crater's bright halo and ray system.
float skyMoonRays(vec3 _p, vec3 _centre, float _seed)
{
	float angle = acos(clamp(dot(_p, _centre), -1.0, 1.0));
	if (angle > 1.2)
	{
		return 0.0;
	}
	vec3 t1 = normalize(cross(_centre, vec3(0.0, 1.0, 0.0)));
	vec3 t2 = cross(_centre, t1);
	float spoke = (atan2(dot(_p, t2), dot(_p, t1)) / (2.0 * SKY_PI) + 0.5) * 24.0;
	float h0 = skyHash(vec3(mod(floor(spoke), 24.0), _seed, 3.0)).x;
	float h1 = skyHash(vec3(mod(floor(spoke) + 1.0, 24.0), _seed, 3.0)).x;
	float spokes = smoothstep(0.55, 0.95, mix(h0, h1, smoothstep(0.0, 1.0, fract(spoke))));
	return spokes * exp(-angle * 3.0) * smoothstep(0.02, 0.06, angle) * 0.6 + exp(-angle * angle / 0.0012);
}

// The moon: a sphere lit by the sun (Lommel-Seeliger, so the full moon looks flat rather than like a
// ball), dark maria, cratered highlands whose relief shows along the terminator, earthshine on its
// night side.
vec4 skyMoonDisc(vec3 _dir, float _pixelAngle)
{
    vec3 moonDir = u_skyMoon.xyz;
    float radius = u_skyMoon.w;
    vec2 screenDisc;
    if (!skyDiscCoordinates(_dir, moonDir, radius, screenDisc)) return vec4_splat(0.0);
    vec3 up = normalize(u_skyMoonUp.xyz - moonDir * dot(u_skyMoonUp.xyz, moonDir));
    vec3 right = normalize(cross(up, moonDir));
    vec2 north = normalize(vec2(dot(up, normalize(u_skyBasisRight.xyz)), dot(up, normalize(u_skyBasisUp.xyz))));
    vec2 disc = vec2(dot(screenDisc, vec2(north.y, -north.x)), dot(screenDisc, north));
    float distance = length(disc);
    float aa = max(fwidth(distance) * 0.65, 1e-4);
    float edge = 1.0 - smoothstep(1.0 - aa, 1.0 + aa, distance);
    if (edge <= 0.0) return vec4_splat(0.0);
	// The visible hemisphere in the moon's own frame (it always shows us the same face).
	vec3 surface = distance < 1.0 ? vec3(disc, sqrt(1.0 - distance * distance)) : vec3(disc / distance, 0.0);

	// Ragged shorelines: two octaves of noise push the basins' edges in and out.
	float shore = skyMoonNoise(surface * 7.0 + vec3(3.1, 1.7, 6.3))
		+ 0.5 * skyMoonNoise(surface * 21.0 + vec3(6.0, 4.0, 2.0)) - 0.75;
	float mare = skyMaria(surface, shore * 0.55);
	// Finer relief only where the disc is big enough on screen not to shimmer.
	float detail = saturate(radius / max(_pixelAngle, 1e-6) / 30.0 - 0.3);
	vec4 craters = skyMoonCraters(surface, 4.0, 0.0) + skyMoonCraters(surface, 11.0, 17.0) * (0.7 * detail);
	craters *= mix(0.25, 0.08, mare);
	float speckle = skyMoonNoise(surface * 32.0 + vec3(2.0, 6.0, 4.0));
	float rays = skyMoonRays(surface, vec3(-0.1414, -0.6858, 0.7139), 1.0)   // Tycho
		+ skyMoonRays(surface, vec3(-0.3372, 0.1668, 0.9265), 7.0) * 0.6      // Copernicus
		+ skyMoonRays(surface, vec3(-0.6095, 0.1409, 0.7801), 13.0) * 0.4;    // Kepler
	float albedo = (mix(0.8, 0.42, mare) * mix(0.88, 1.1, speckle) + 0.22 * rays) * (1.0 + clamp(craters.w * 6.0, -0.06, 0.15));

	// Craters tilt the surface: only the slope across the sphere matters, foreshortened at the limb.
	vec3 slope = (craters.xyz - surface * dot(craters.xyz, surface)) * smoothstep(0.1, 0.6, surface.z);
	vec3 bodyNormal = normalize(surface - slope * 0.8);
	vec3 normal = right * bodyNormal.x + up * bodyNormal.y - moonDir * bodyNormal.z;

	// Relief shows along the terminator but doesn't light peaks scattered over the night side.
	float smoothMu0 = dot(right * surface.x + up * surface.y - moonDir * surface.z, u_skySun.xyz);
	float mu0 = saturate(dot(normal, u_skySun.xyz)) * smoothstep(-0.02, 0.08, smoothMu0);
	float mu = max(bodyNormal.z, 0.05);
	float sunlit = 2.0 * mu0 / (mu0 + mu);
	vec3 tint = vec3(1.0, 0.96, 0.9);
	return vec4(tint * albedo * (sunlit + u_skyMoonUp.w) * u_skyMoonColor.w * edge, edge);
}

// The sun's disc, darker and redder towards its limb.
vec3 skySunDisc(vec3 _dir, float _pixelAngle)
{
    vec2 disc;
    if (!skyDiscCoordinates(_dir, u_skySun.xyz, u_skySun.w, disc)) return vec3_splat(0.0);
    float t = length(disc);
    float aa = max(fwidth(t) * 0.65, 1e-4);
    float coverage = 1.0 - smoothstep(1.0 - aa, 1.0 + aa, t);
	float mu = sqrt(max(1.0 - t * t, 0.0));
	vec3 limb = vec3_splat(1.0) - vec3(0.45, 0.55, 0.68) * (1.0 - mu);
	return u_skySunColor.rgb * u_skySunColor.w * limb * coverage;
}

void main()
{
	vec3 dir = normalize(v_skyDir);
    // Derivatives must be evaluated before branches over the horizon, moon and darkness.
    vec3 celestial = vec3(dot(dir, u_skyStarsX.xyz), dot(dir, u_skyStarsY.xyz), dot(dir, u_skyStarsZ.xyz));
    vec3 starDx = dFdx(celestial);
    vec3 starDy = dFdy(celestial);
    vec4 moon = skyMoonDisc(dir, u_skyStarsZ.w);
    vec3 sun = skySunDisc(dir, u_skyStarsZ.w);
    float r = skyCameraRadius();
	vec3 color = skyViewLuminance(dir) + skyNightGlow(dir);

	// What lies beyond the atmosphere: dimmed by the air in front of it, hidden below the horizon.
	vec3 transmittance = skyTransmittance(r, dir.y) * skyPlanetShadow(r, dir.y, 1e-3);
	if (dot(transmittance, vec3_splat(1.0)) > 1e-4)
	{
		// Stars and the galaxy drown in a bright sky (daylight, twilight, the moon's glow).
		float skyLuminance = dot(color, vec3(0.2126, 0.7152, 0.0722));
		float darkness = saturate(1.0 - skyLuminance * 25.0);
		vec3 space = vec3_splat(0.0);
		if (darkness > 0.0)
		{
			space += (skyStars(celestial, starDx, starDy, u_skyStarsY.w) * u_skyStarsX.w + skyMilkyWay(dir, celestial)) * (darkness * darkness);
		}
        // Even its unlit hemisphere is opaque: no stars or sunlight shine through the moon.
        // Keeping solar disc radiance separate from its uncovered area makes partial eclipses
        // reveal a bright crescent, instead of dimming the whole solar disc.
        space = (space + sun) * (1.0 - moon.a) + moon.rgb;
        float totality = 1.0 - smoothstep(0.0, 0.015, u_skyScattering.z);
        float sunAngle = atan2(length(cross(dir, u_skySun.xyz)), dot(dir, u_skySun.xyz));
        float corona = exp(-max(sunAngle / u_skySun.w - 1.0, 0.0) * 2.0);
        space += vec3(0.12, 0.15, 0.2) * corona * totality * (1.0 - moon.a);
		color += space * transmittance;
	}

	if (u_skyCamera.w > 0.5)
	{
		vec4 clouds = texture2DLod(s_skyClouds, gl_FragCoord.xy * u_viewTexel.xy, 0.0);
		color = color * clouds.a + clouds.rgb;
	}
	gl_FragColor = vec4(color, 1.0);
}
