$input v_worldPos, v_portalOrigin, v_portalRight, v_portalUp

#include "Common.sh"

// PortalMaterial: another camera's linear HDR image, read at this pixel's screen position.
SAMPLER2D(s_texDiffuse, 0);
uniform vec4 u_screenView; // rgb tint (sRGB) shown while there is no image, w = 1 with an image

void main()
{
    // Once the eye is close to the face, its near plane cuts that face away. The
    // back of the coverage volume must still win depth against the host wall;
    // using the box's physical depth produced a flash of wall before transfer.
    vec3 normal = normalize(cross(v_portalRight, v_portalUp));
    vec3 eye = u_invView[3].xyz;
    vec3 ray = v_worldPos - eye;
    float denominator = dot(ray, normal);
    if (abs(denominator) < 0.000001) discard;
    float distance = dot(v_portalOrigin - eye, normal) / denominator;
    if (distance < 0.0) discard;
    vec3 portalPoint = eye + ray * distance;
    vec3 offset = portalPoint - v_portalOrigin;
    if (abs(dot(offset, v_portalRight)) > dot(v_portalRight, v_portalRight)
        || abs(dot(offset, v_portalUp)) > dot(v_portalUp, v_portalUp)) discard;
    vec4 projected = mul(u_viewProj, vec4(portalPoint, 1.0));
    float depth = projected.z / max(projected.w, 0.000001);
#if BGFX_SHADER_LANGUAGE_GLSL
    depth = depth * 0.5 + 0.5;
#endif
    gl_FragDepth = clamp(depth, 0.0, 1.0);
	vec2 uv = (gl_FragCoord.xy - u_viewRect.xy) / u_viewRect.zw;
	vec3 view = texture2D(s_texDiffuse, uv).rgb;
	gl_FragColor = vec4(mix(toLinear(u_screenView.rgb), view, u_screenView.w), 1.0);
}
