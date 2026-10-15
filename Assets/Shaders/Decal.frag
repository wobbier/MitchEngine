#include "Common.sh"
#include "Lighting.sh"

SAMPLER2D(s_decalAlbedo, 0);
SAMPLER2D(s_decalNormal, 1);
SAMPLER2D(s_sceneDepth, 2);
uniform vec4 u_decalColor;
uniform vec4 u_decalEmissive;
uniform vec4 u_decalSurface;
uniform vec4 u_decalDepth;
uniform mat4 u_decalWorldToLocal;
uniform mat4 u_decalAxes;

void main()
{
    vec2 screenUV = (gl_FragCoord.xy - u_viewRect.xy) / u_viewRect.zw;
    float depth = texture2D(s_sceneDepth, screenUV).r;
    if (depth >= 1.0) discard;
    vec2 ndc = screenUV * 2.0 - 1.0;
    if (u_clusterGrid.w < 0.5) ndc.y = -ndc.y;
    float z = u_decalDepth.w > 0.5 ? depth * 2.0 - 1.0 : depth;
    vec4 viewPos = mul(u_invProj, vec4(ndc, z, 1.0));
    viewPos /= viewPos.w;
    vec3 worldPos = mul(u_invView, viewPos).xyz;
    vec3 local = mul(u_decalWorldToLocal, vec4(worldPos, 1.0)).xyz;
    if (any(greaterThan(abs(local), vec3_splat(0.5)))) discard;

    vec3 cameraPos = mul(u_invView, vec4(0.0, 0.0, 0.0, 1.0)).xyz;
    vec3 V = normalize(cameraPos - worldPos);
    vec3 N = normalize(cross(dFdx(worldPos), dFdy(worldPos)));
    if (dot(N, V) < 0.0) N = -N;
    vec3 projector = mul(u_decalAxes, vec4(0.0, 0.0, 1.0, 0.0)).xyz;
    float fade = clamp((dot(N, -normalize(projector)) - u_decalSurface.w) / max(1.0 - u_decalSurface.w, 1e-4), 0.0, 1.0);
    vec2 uv = vec2(local.x + 0.5, 0.5 - local.y);
    vec4 base = toLinear(texture2D(s_decalAlbedo, uv)) * u_decalColor;
    float alpha = base.a * fade;
    if (alpha <= 0.001) discard;
    vec2 normalXY = (texture2D(s_decalNormal, uv).xy * 2.0 - 1.0) * u_decalSurface.z;
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
