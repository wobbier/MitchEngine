#ifndef ERA_SH
#define ERA_SH

// Era effects on Standard materials (LOOK-S11), per camera through the lighting state:
// x: vertex snap grid height in virtual pixels (0 = off), y: 1 = affine textures, z: 1 = three-point.
uniform vec4 u_eraParams;

// PS1 wobble: the projected vertex lands on a coarse screen grid.
vec4 eraSnapVertex(vec4 _clip)
{
	if (u_eraParams.x > 0.5 && _clip.w > 0.0)
	{
		vec2 grid = vec2(u_eraParams.x * u_viewRect.z / max(u_viewRect.w, 1.0), u_eraParams.x) * 0.5;
		vec2 ndc = _clip.xy / _clip.w;
		ndc = floor(ndc * grid + 0.5) / grid;
		_clip.xy = ndc * _clip.w;
	}
	return _clip;
}

// N64 three-point filtering: the triangle of the three nearest texels instead of four.
vec4 eraThreePoint(sampler2D _sampler, vec2 _uv)
{
	vec2 size = vec2(textureSize(_sampler, 0));
	vec2 texel = 1.0 / max(size, vec2_splat(1.0));
	vec2 p = _uv * size - 0.5;
	vec2 f = fract(p);
	vec2 base = (floor(p) + 0.5) * texel;
	vec4 a = texture2D(_sampler, base);
	vec4 b = texture2D(_sampler, base + vec2(texel.x, 0.0));
	vec4 c = texture2D(_sampler, base + vec2(0.0, texel.y));
	vec4 d = texture2D(_sampler, base + texel);
	if (f.x + f.y <= 1.0)
	{
		return a + f.x * (b - a) + f.y * (c - a);
	}
	return d + (1.0 - f.x) * (c - d) + (1.0 - f.y) * (b - d);
}

#endif
