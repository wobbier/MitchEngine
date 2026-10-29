$input v_texcoord0, v_color0, v_normal, v_tangent, v_viewPos, v_worldPos

// Sprites in linear HDR, after transparents. Like particles, no depth buffer is bound (the scene
// depth is read as a texture), so the depth test against the opaque scene happens here. Unlit
// sprites show their texture times the tint; lit ones are rough, non-metallic surfaces facing
// the viewer, shaded by the sun, clustered lights and ambient like any surface. Both get forward fog.
#include "Common.sh"
#include "StandardSurface.sh"

SAMPLER2D(s_texSprite, 0);
SAMPLER2D(s_sceneDepth, 1);
SAMPLER2D(s_texSpriteNormal, 2);

uniform vec4 u_spriteParams;   // x 1 = lit, y 1 = additive (premultiply), z 1 = normal mapped, w 1 = SDF alpha (text)
uniform vec4 u_particleDepth;  // x P[2][2], y P[3][2], z orthographic, w depth range -1..1

float sceneViewDepth(vec2 _uv)
{
	float d = texture2DLod(s_sceneDepth, _uv, 0.0).r;
	float ndc = u_particleDepth.w > 0.5 ? d * 2.0 - 1.0 : d;
	if (u_particleDepth.z > 0.5)
	{
		return (ndc - u_particleDepth.y) / u_particleDepth.x;
	}
	return u_particleDepth.y / (ndc - u_particleDepth.x);
}

void main()
{
	vec4 tex = texture2D(s_texSprite, v_texcoord0);
	if (u_spriteParams.w > 0.5)
	{
		// Signed distance glyphs: a pixel-wide antialiased edge at any scale.
		float width = max(fwidth(tex.a) * 0.75, 0.0001);
		tex.a = smoothstep(0.5 - width, 0.5 + width, tex.a);
	}
	vec4 color = vec4(toLinear(tex.rgb), tex.a) * v_color0;
	if (color.a < 0.002)
	{
		discard;
	}
	if (sceneViewDepth(gl_FragCoord.xy * u_viewTexel.xy) <= v_viewPos.z)
	{
		discard;
	}
	vec3 cameraPos = mul(u_invView, vec4(0.0, 0.0, 0.0, 1.0)).xyz;
	if (u_spriteParams.x > 0.5)
	{
		// Two-sided: the face toward the viewer.
		vec3 normal = normalize(v_normal);
		normal = dot(normal, cameraPos - v_worldPos) < 0.0 ? -normal : normal;
		if (u_spriteParams.z > 0.5)
		{
			// Tangent space: red along the image's +u, green its up, blue out of the sprite (z
			// rebuilt, so two-channel BC5 maps work too).
			vec3 tangentNormal;
			tangentNormal.xy = texture2D(s_texSpriteNormal, v_texcoord0).xy * 2.0 - 1.0;
			tangentNormal.z = sqrt(max(1.0 - dot(tangentNormal.xy, tangentNormal.xy), 0.0));
			vec3 T = normalize(v_tangent.xyz);
			vec3 B = cross(normal, T) * v_tangent.w;
			normal = normalize(T * tangentNormal.x + B * tangentNormal.y + normal * tangentNormal.z);
		}
		SurfaceInput surface;
		surface.baseColor = color.rgb;
		surface.alpha = color.a;
		surface.normal = normal;
		surface.metallic = 0.0;
		surface.roughness = 1.0;
		surface.occlusion = 1.0;
		surface.emissive = vec3_splat(0.0);
		surface.uv = v_texcoord0;
		color = shadeStandardSurface(surface, v_worldPos, normal, v_viewPos, gl_FragCoord.xy, true);
	}
	else
	{
		vec3 sunColor = u_lightParams.x > 0.5 ? directionalRadiance(0, v_worldPos) : vec3_splat(0.0);
		vec4 fog = fogForward(cameraPos, v_worldPos, sunColor, u_dirLightDirection[0].xyz);
		color.rgb = u_spriteParams.y > 0.5 ? color.rgb * fog.a : color.rgb * fog.a + fog.rgb;
	}
	gl_FragColor = u_spriteParams.y > 0.5 ? vec4(color.rgb * color.a, color.a) : color;
}
