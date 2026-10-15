#include "Common.sh"
#include "Lighting.sh"

SAMPLER2D(s_decalAlbedo, 0);
SAMPLER2D(s_decalNormal, 1);
SAMPLER2D(s_sceneDepth, 2);
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
    float depth = texture2D(s_sceneDepth, _screenUV).r;
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

void main()
{
    vec2 screenUV = (gl_FragCoord.xy - u_viewRect.xy) / u_viewRect.zw;
    if (texture2D(s_sceneDepth, screenUV).r >= 1.0) discard;
    vec3 viewPos = sceneViewPosition(screenUV);
    vec3 local = decalLocal(viewPos);
    if (any(greaterThan(abs(local), vec3_splat(0.5)))) discard;
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
    vec3 V = normalize(cameraPos - worldPos);
    vec3 projector = mul(u_decalAxes, vec4(0.0, 0.0, 1.0, 0.0)).xyz;
    // Full coverage on surfaces facing the projector, fading out between the start (u_decalEmissive.w)
    // and end (u_decalSurface.w) angle cosines.
    float facing = dot(N, -normalize(projector));
    float fade = smoothstep(u_decalSurface.w, max(u_decalEmissive.w, u_decalSurface.w + 1e-4), facing);

    vec2 uv = decalUV(local);
    vec2 uvDx = decalUV(decalLocal(viewPos + stepX)) - uv;
    vec2 uvDy = decalUV(decalLocal(viewPos + stepY)) - uv;
    vec4 base = toLinear(texture2DLod(s_decalAlbedo, uv, mipLevel(uvDx, uvDy, u_decalTexSize.xy))) * u_decalColor;
    float alpha = base.a * fade;
    if (alpha <= 0.001) discard;
    vec2 normalXY = (texture2DLod(s_decalNormal, uv, mipLevel(uvDx, uvDy, u_decalTexSize.zw)).xy * 2.0 - 1.0) * u_decalSurface.z;
    vec3 T = mul(u_decalAxes, vec4(1.0, 0.0, 0.0, 0.0)).xyz;
    vec3 B = mul(u_decalAxes, vec4(0.0, 1.0, 0.0, 0.0)).xyz;
    T = normalize(T - N * dot(T, N));
    B = normalize(B - N * dot(B, N));
    N = normalize(T * normalXY.x + B * normalXY.y + N * sqrt(max(1.0 - dot(normalXY, normalXY), 0.0)));

    Surface s;
    s.position = worldPos;
    s.view = V;
    s.normal = N;
    s.diffuseColor = base.rgb * (1.0 - u_decalSurface.x);
    s.f0 = mix(vec3_splat(0.04), base.rgb, u_decalSurface.x);
    s.roughness = clamp(u_decalSurface.y, 0.045, 1.0);
    s.NoV = max(dot(N, V), 1e-4);
    vec3 color = ambientLighting(s, 1.0) + u_decalEmissive.rgb;
    for (int i = 0; i < MAX_DIRECTIONAL_LIGHTS; ++i)
    {
        if (float(i) >= u_lightParams.x) break;
        vec3 L = -u_dirLightDirection[i].xyz;
        float visibility = i == 0 && u_dirLightDirection[0].w > 0.5 ? sunShadow(s, L, viewPos.z) : 1.0;
        color += shadeLight(s, L, directionalRadiance(i, s.position)) * visibility;
    }
    color += shadeClusteredLights(s, gl_FragCoord.xy, viewPos.z);
    gl_FragColor = vec4(color, alpha);
}
