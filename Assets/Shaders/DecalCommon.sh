#ifndef DECAL_COMMON_SH
#define DECAL_COMMON_SH

// Deferred-style box decals, shared by Decal.frag and shader graph decals: rebuilds the surface
// under the pixel from the scene depth (DECAL_SCENE_DEPTH: define it to the depth sampler before
// including), maps it into the decal box, and lights the decal's surface like Standard.frag.
#include "Lighting.sh"

uniform vec4 u_decalColor;
uniform vec4 u_decalEmissive;
uniform vec4 u_decalSurface;
uniform vec4 u_decalDepth;
uniform vec4 u_decalTexSize;    // texels: xy albedo, zw normal map
uniform mat4 u_decalWorldToLocal;
uniform mat4 u_decalAxes;

// View-space position of the opaque surface under a screen position.
vec3 sceneViewPosition(vec2 _screenUV)
{
    float depth = texture2DLod(DECAL_SCENE_DEPTH, _screenUV, 0.0).r;
    vec2 ndc = _screenUV * 2.0 - 1.0;
    if (u_clusterGrid.w < 0.5) ndc.y = -ndc.y;
    float z = u_decalDepth.w > 0.5 ? depth * 2.0 - 1.0 : depth;
    vec4 viewPos = mul(u_invProj, vec4(ndc, z, 1.0));
    return viewPos.xyz / viewPos.w;
}

vec3 decalLocal(vec3 _viewPos)
{
    return mul(u_decalWorldToLocal, mul(u_invView, vec4(_viewPos, 1.0))).xyz;
}

// The texture's right is the box's X, its up the box's Y.
vec2 decalUV(vec3 _local)
{
    return vec2(_local.x + 0.5, 0.5 - _local.y);
}

float mipLevel(vec2 _uvDx, vec2 _uvDy, vec2 _size)
{
    vec2 x = _uvDx * _size;
    vec2 y = _uvDy * _size;
    return max(0.5 * log2(max(max(dot(x, x), dot(y, y)), 1e-8)), 0.0);
}

struct DecalGeometry
{
    vec3 viewPos;
    vec3 worldPos;
    vec3 local;      // box space, -0.5..0.5
    vec3 normal;     // the surface's world normal (from depth)
    vec3 tangent;    // the box's X and Y on the surface
    vec3 bitangent;
    vec3 view;       // towards the camera
    vec2 uv;
    vec2 uvDx;       // one-pixel UV steps (texture footprint)
    vec2 uvDy;
    float fade;      // angle fade 0..1
};

// False when the pixel's surface is outside the box (or is the sky): discard it.
bool decalGeometry(vec2 _fragCoord, out DecalGeometry _g)
{
    _g.viewPos = vec3_splat(0.0);
    _g.worldPos = vec3_splat(0.0);
    _g.local = vec3_splat(0.0);
    _g.normal = vec3(0.0, 1.0, 0.0);
    _g.tangent = vec3(1.0, 0.0, 0.0);
    _g.bitangent = vec3(0.0, 0.0, 1.0);
    _g.view = vec3(0.0, 0.0, 1.0);
    _g.uv = vec2_splat(0.0);
    _g.uvDx = vec2_splat(0.0);
    _g.uvDy = vec2_splat(0.0);
    _g.fade = 0.0;

    vec2 screenUV = (_fragCoord - u_viewRect.xy) / u_viewRect.zw;
    if (texture2DLod(DECAL_SCENE_DEPTH, screenUV, 0.0).r >= 1.0) return false;
    vec3 viewPos = sceneViewPosition(screenUV);
    vec3 local = decalLocal(viewPos);
    if (any(greaterThan(abs(local), vec3_splat(0.5)))) return false;
    vec3 worldPos = mul(u_invView, vec4(viewPos, 1.0)).xyz;

    // One-pixel steps towards the neighbour on the flatter side in each screen axis. They give a
    // per-pixel surface normal and texture footprint: 2x2 quad derivatives would light curved
    // surfaces in blocks and drop to the smallest mip along every depth edge inside the box.
    vec2 dx = vec2(u_viewTexel.x, 0.0);
    vec2 dy = vec2(0.0, u_viewTexel.y);
    vec3 left = sceneViewPosition(screenUV - dx);
    vec3 right = sceneViewPosition(screenUV + dx);
    vec3 below = sceneViewPosition(screenUV - dy);
    vec3 above = sceneViewPosition(screenUV + dy);
    vec3 stepX = abs(right.z - viewPos.z) < abs(viewPos.z - left.z) ? right - viewPos : viewPos - left;
    vec3 stepY = abs(above.z - viewPos.z) < abs(viewPos.z - below.z) ? above - viewPos : viewPos - below;
    vec3 viewNormal = normalize(cross(stepY, stepX));
    if (dot(viewNormal, viewPos) > 0.0) viewNormal = -viewNormal;
    vec3 N = normalize(mul(u_invView, vec4(viewNormal, 0.0)).xyz);

    vec3 cameraPos = mul(u_invView, vec4(0.0, 0.0, 0.0, 1.0)).xyz;
    vec3 projector = mul(u_decalAxes, vec4(0.0, 0.0, 1.0, 0.0)).xyz;
    // Full coverage on surfaces facing the projector, fading out between the start (u_decalEmissive.w)
    // and end (u_decalSurface.w) angle cosines.
    float facing = dot(N, -normalize(projector));
    vec3 T = mul(u_decalAxes, vec4(1.0, 0.0, 0.0, 0.0)).xyz;
    vec3 B = mul(u_decalAxes, vec4(0.0, 1.0, 0.0, 0.0)).xyz;

    _g.viewPos = viewPos;
    _g.worldPos = worldPos;
    _g.local = local;
    _g.normal = N;
    _g.tangent = normalize(T - N * dot(T, N));
    _g.bitangent = normalize(B - N * dot(B, N));
    _g.view = normalize(cameraPos - worldPos);
    _g.uv = decalUV(local);
    _g.uvDx = decalUV(decalLocal(viewPos + stepX)) - _g.uv;
    _g.uvDy = decalUV(decalLocal(viewPos + stepY)) - _g.uv;
    _g.fade = smoothstep(u_decalSurface.w, max(u_decalEmissive.w, u_decalSurface.w + 1e-4), facing);
    return true;
}

// A tangent-space normal (x along the box's X, y along its Y) on the decal's surface.
vec3 decalWorldNormal(DecalGeometry _g, vec3 _tangentNormal)
{
    return normalize(_g.tangent * _tangentNormal.x + _g.bitangent * _tangentNormal.y + _g.normal * _tangentNormal.z);
}

vec3 shadeDecal(DecalGeometry _g, vec3 _base, vec3 _normal, float _metallic, float _roughness, vec3 _emissive, vec2 _fragCoord)
{
    Surface s;
    s.position = _g.worldPos;
    s.view = _g.view;
    s.normal = _normal;
    s.diffuseColor = _base * (1.0 - _metallic);
    s.f0 = mix(vec3_splat(0.04), _base, _metallic);
    s.roughness = clamp(_roughness, 0.045, 1.0);
    s.NoV = max(dot(_normal, _g.view), 1e-4);
    vec3 color = ambientLighting(s, 1.0) + _emissive;
    for (int i = 0; i < MAX_DIRECTIONAL_LIGHTS; ++i)
    {
        if (float(i) >= u_lightParams.x) break;
        vec3 L = -u_dirLightDirection[i].xyz;
        float visibility = i == 0 && u_dirLightDirection[0].w > 0.5 ? sunShadow(s, L, _g.viewPos.z) : 1.0;
        color += shadeLight(s, L, directionalRadiance(i, s.position)) * visibility;
    }
    color += shadeClusteredLights(s, _fragCoord, _g.viewPos.z);
    return color;
}

#endif // DECAL_COMMON_SH
