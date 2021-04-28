$input v_color0, v_normal, v_texcoord0

#include "Common.sh"

SAMPLER2D(s_texDiffuse, 0);
SAMPLER2D(s_texNormal, 1);
SAMPLER2D(s_texAlpha, 2);
uniform vec4 s_diffuse;
uniform vec4 s_ambient;
uniform vec4 s_sunDirection;
uniform vec4 s_sunDiffuse;
uniform vec4 s_tiling;
uniform vec4 u_skyLuminance;

void main()
{
	vec2 uvs = v_texcoord0 * s_tiling.xy;
	vec4 color = texture2D(s_texDiffuse, uvs) * s_diffuse;

	vec4 ambient = s_ambient * color;
	vec4 lightDir = normalize(s_sunDirection);
	vec3 skyDirection = vec3(0.0, 0.0, 1.0);

	float diff = max(dot(normalize(v_normal), lightDir), 0.0);
	float diffuseSky = 1.0 + 0.5 * dot(normalize(v_normal), skyDirection);
	
	vec4 diffuse = diff * s_sunDiffuse;// * color;
	color += diffuse;
	color += diffuseSky * u_skyLuminance;
	color.a = toLinear(texture2D(s_texAlpha, uvs)).r;
	gl_FragColor = color;//texture2D(s_texNormal, uvs);//color;ambient + 
}