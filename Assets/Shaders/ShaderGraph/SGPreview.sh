#ifndef SHADER_GRAPH_PREVIEW_SH
#define SHADER_GRAPH_PREVIEW_SH

// Shader graph editor preview helpers: display conversion, a fixed light rig for the graph's
// result, and stand-ins for scene inputs.

// Linear values as they'd look on screen (clamped, sRGB encoded).
vec4 sgPreviewDisplay(vec3 _linear)
{
	return vec4(sgLinearToSRGB(clamp(_linear, 0.0, 1.0)), 1.0);
}

vec3 sgPreviewSky(vec3 _direction)
{
	float up = _direction.y * 0.5 + 0.5;
	return mix(vec3(0.18, 0.16, 0.14), vec3(0.55, 0.68, 0.95), smoothstep(0.35, 0.65, up));
}

// A metallic-roughness surface under one key light and a sky (not the scene's lighting: the
// scene view shows that).
vec4 sgPreviewLit(vec3 _base, vec3 _normal, float _metallic, float _roughness, vec3 _emission, float _occlusion)
{
	vec3 N = sgSafeNormalize(_normal);
	vec3 V = vec3(0.0, 0.0, 1.0);
	vec3 L = normalize(vec3(-0.45, 0.65, 0.6));
	vec3 H = normalize(L + V);
	float metallic = clamp(_metallic, 0.0, 1.0);
	float roughness = clamp(_roughness, 0.045, 1.0);
	float a = roughness * roughness;
	float NoL = max(dot(N, L), 0.0);
	float NoV = max(dot(N, V), 1e-4);
	float NoH = max(dot(N, H), 0.0);
	float VoH = max(dot(V, H), 0.0);
	vec3 f0 = mix(vec3_splat(0.04), _base, metallic);
	vec3 diffuse = _base * (1.0 - metallic);

	float a2 = a * a;
	float d = NoH * NoH * (a2 - 1.0) + 1.0;
	float D = a2 / (3.14159265 * d * d);
	float k = a * 0.5;
	float Vis = 0.25 / ((NoL * (1.0 - k) + k) * (NoV * (1.0 - k) + k));
	vec3 F = f0 + (vec3_splat(1.0) - f0) * pow(1.0 - VoH, 5.0);
	vec3 direct = (diffuse / 3.14159265 + D * Vis * F) * NoL * 3.2;

	vec3 Fv = f0 + (max(vec3_splat(1.0 - roughness), f0) - f0) * pow(1.0 - NoV, 5.0);
	vec3 R = reflect(-V, N);
	vec3 ambient = diffuse * sgPreviewSky(N) * (vec3_splat(1.0) - Fv) + Fv * sgPreviewSky(R) * (1.0 - roughness * 0.6);
	vec3 color = direct + ambient * _occlusion + _emission;
	// Reinhard on luminance keeps hue for the display.
	color = color / (1.0 + sgLuminance(color));
	return vec4(sgLinearToSRGB(clamp(color, 0.0, 1.0)), 1.0);
}

// Stand-ins for the scene inputs post-process / decal / particle graphs read.
vec3 sgSceneColor(vec2 _uv)
{
	vec3 sky = mix(vec3(0.62, 0.74, 0.95), vec3(0.2, 0.33, 0.62), clamp(_uv.y * 1.6, 0.0, 1.0));
	vec2 checks = floor(_uv * vec2(8.0, 6.0));
	float checker = mod(checks.x + checks.y, 2.0);
	vec3 ground = mix(vec3(0.35, 0.3, 0.25), vec3(0.55, 0.5, 0.42), checker);
	return _uv.y < 0.55 ? sky * (1.4 - _uv.y) : ground;
}

float sgSceneDepth(vec2 _uv)
{
	return _uv.y < 0.55 ? 1000.0 : mix(30.0, 2.0, (_uv.y - 0.55) / 0.45);
}

#endif // SHADER_GRAPH_PREVIEW_SH
