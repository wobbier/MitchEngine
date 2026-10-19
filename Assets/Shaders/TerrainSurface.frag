$input v_worldPos, v_normal, v_tangent, v_bitangent, v_texcoord0, v_viewPos
#include "Common.sh"
#include "StandardSurface.sh"

SAMPLER2D(s_terrainWeights, 0);
uniform vec4 u_terrainLayers[4]; // linear rgb, roughness
uniform vec4 u_terrainSurface;   // enabled, sample-center UV scale, sample-center UV offset
uniform vec4 u_baseColor;
uniform vec4 u_pbrParams;

void main()
{
    vec4 weights = texture2D(s_terrainWeights, v_texcoord0 * u_terrainSurface.y + u_terrainSurface.z);
    float sum = dot(weights, vec4(1.0));
    weights = sum > 0.00001 ? weights / sum : vec4(1.0, 0.0, 0.0, 0.0);
    vec4 layer = u_terrainLayers[0] * weights.x + u_terrainLayers[1] * weights.y
        + u_terrainLayers[2] * weights.z + u_terrainLayers[3] * weights.w;
    SurfaceInput surface;
    surface.baseColor = mix(u_baseColor.rgb, layer.rgb, u_terrainSurface.x);
    surface.alpha = 1.0;
    surface.normal = normalize(v_normal);
    surface.metallic = 0.0;
    surface.roughness = mix(u_pbrParams.y, layer.a, u_terrainSurface.x);
    surface.occlusion = 1.0;
    surface.emissive = vec3(0.0);
    surface.uv = v_texcoord0;
    gl_FragColor = shadeStandardSurface(surface, v_worldPos, v_normal, v_viewPos, gl_FragCoord.xy, gl_FrontFacing);
}
