#include "Common.sh"

SAMPLER2D(s_decalAlbedo, 0);
SAMPLER2D(s_decalNormal, 1);
SAMPLER2D(s_sceneDepth, 2);
#define DECAL_SCENE_DEPTH s_sceneDepth
#include "DecalCommon.sh"

void main()
{
    DecalGeometry g;
    if (!decalGeometry(gl_FragCoord.xy, g)) discard;

    vec4 base = toLinear(texture2DLod(s_decalAlbedo, g.uv, mipLevel(g.uvDx, g.uvDy, u_decalTexSize.xy))) * u_decalColor;
    float alpha = base.a * g.fade;
    if (alpha <= 0.001) discard;
    vec2 normalXY = (texture2DLod(s_decalNormal, g.uv, mipLevel(g.uvDx, g.uvDy, u_decalTexSize.zw)).xy * 2.0 - 1.0) * u_decalSurface.z;
    vec3 N = decalWorldNormal(g, vec3(normalXY, sqrt(max(1.0 - dot(normalXY, normalXY), 0.0))));

    vec3 color = shadeDecal(g, base.rgb, N, u_decalSurface.x, u_decalSurface.y, u_decalEmissive.rgb, gl_FragCoord.xy);
    gl_FragColor = vec4(color, alpha);
}
