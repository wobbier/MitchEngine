#include "Common.sh"
#include "StandardSurface.sh"

SAMPLER2D(s_terrainWeights, 0);
#if TERRAIN_ARRAYS
SAMPLER2DARRAY(s_terrainBase, 1);
SAMPLER2DARRAY(s_terrainNormal, 2);
SAMPLER2DARRAY(s_terrainOrm, 3);
#else
SAMPLER2D(s_terrainBase, 1);
SAMPLER2D(s_terrainNormal, 2);
SAMPLER2D(s_terrainOrm, 3);
#endif
uniform vec4 u_terrainLayers[4];  // linear rgb tint, roughness factor
uniform vec4 u_terrainMapping[4]; // inverse tile metres, normal strength, metallic factor, normal Y sign
uniform vec4 u_terrainSurface;    // layers enabled, sample-center UV scale, sample-center UV offset
uniform vec4 u_terrainTextures;   // textures enabled, atlas cell size, last independent mip
uniform vec4 u_terrainOrigin;     // terrain instance world X/Z translation
uniform vec4 u_baseColor;
uniform vec4 u_pbrParams;

#if !TERRAIN_ARRAYS
vec2 terrainAtlasUv(vec2 uv, float layer, float lod)
{
    float cell = u_terrainTextures.y * exp2(-lod);
    float gutter = cell >= 8.0 ? 2.0 : 1.0;
    vec2 tile = vec2(mod(layer, 2.0), floor(layer * 0.5));
    return (tile * cell + gutter + fract(uv) * (cell - 2.0 * gutter)) / (cell * 2.0);
}
#endif

void main()
{
    vec4 weights = texture2D(s_terrainWeights, v_texcoord0 * u_terrainSurface.y + u_terrainSurface.z);
    float sum = dot(weights, vec4(1.0));
    weights = sum > 0.00001 ? weights / sum : vec4(1.0, 0.0, 0.0, 0.0);
    vec3 color = vec3(0.0), tangentNormal = vec3(0.0);
    float roughness = 0.0, metallic = 0.0, occlusion = 0.0;
    vec2 local = v_worldPos.xz - u_terrainOrigin.xy;
    for (int i = 0; i < 4; ++i)
    {
        vec4 base = vec4(1.0), normal = vec4(0.5, 0.5, 1.0, 1.0), orm = vec4(1.0);
        if (u_terrainTextures.x > 0.5)
        {
            vec2 uv = local * u_terrainMapping[i].x;
#if TERRAIN_ARRAYS
            base = texture2DArray(s_terrainBase, vec3(uv, float(i)));
            normal = texture2DArray(s_terrainNormal, vec3(uv, float(i)));
            orm = texture2DArray(s_terrainOrm, vec3(uv, float(i)));
#else
            // Derivatives use continuous terrain-local UVs, before repeat/atlas remapping.
            vec2 dx = dFdx(uv) * (u_terrainTextures.y - 4.0);
            vec2 dy = dFdy(uv) * (u_terrainTextures.y - 4.0);
            float lod = clamp(0.5 * log2(max(max(dot(dx, dx), dot(dy, dy)), 1.0)), 0.0, u_terrainTextures.z);
            float low = floor(lod), high = min(low + 1.0, u_terrainTextures.z);
            vec2 a = terrainAtlasUv(uv, float(i), low), b = terrainAtlasUv(uv, float(i), high);
            base = mix(texture2DLod(s_terrainBase, a, low), texture2DLod(s_terrainBase, b, high), fract(lod));
            normal = mix(texture2DLod(s_terrainNormal, a, low), texture2DLod(s_terrainNormal, b, high), fract(lod));
            orm = mix(texture2DLod(s_terrainOrm, a, low), texture2DLod(s_terrainOrm, b, high), fract(lod));
#endif
        }
        color += toLinearAccurate(base.rgb) * u_terrainLayers[i].rgb * weights[i];
        roughness += orm.g * u_terrainLayers[i].a * weights[i];
        metallic += orm.b * u_terrainMapping[i].z * weights[i];
        occlusion += orm.r * weights[i];
        vec2 xy = (normal.xy * 2.0 - 1.0) * u_terrainMapping[i].y;
        xy.y *= u_terrainMapping[i].w;
        tangentNormal += normalize(vec3(xy, sqrt(max(1.0 - dot(xy, xy), 0.0)))) * weights[i];
    }
    if (dot(tangentNormal, tangentNormal) < 1e-6) tangentNormal = vec3(0.0, 0.0, 1.0);
    vec3 N = normalize(v_normal);
    if (dot(v_tangent, v_tangent) > 1e-6 && dot(v_bitangent, v_bitangent) > 1e-6)
    {
        mat3 TBN = mtxFromCols(normalize(v_tangent), normalize(v_bitangent), N);
        N = normalize(mul(TBN, tangentNormal));
    }
    SurfaceInput surface;
    surface.baseColor = mix(u_baseColor.rgb, color, u_terrainSurface.x);
    surface.alpha = 1.0;
    surface.normal = normalize(mix(normalize(v_normal), N, u_terrainSurface.x));
    surface.metallic = metallic * u_terrainSurface.x;
    surface.roughness = mix(u_pbrParams.y, roughness, u_terrainSurface.x);
    surface.occlusion = mix(1.0, occlusion, u_terrainSurface.x);
    surface.emissive = vec3(0.0);
    surface.uv = v_texcoord0;
    gl_FragColor = shadeStandardSurface(surface, v_worldPos, v_normal, v_viewPos, gl_FragCoord.xy, gl_FrontFacing);
}
