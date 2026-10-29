#ifndef DITHER_SH_HEADER_GUARD
#define DITHER_SH_HEADER_GUARD

// Screen-door fades (ME-279): opaque surfaces fade without sorting by dropping texels against a
// 4x4 Bayer pattern. A fade d in (0, 1] drops the texels whose threshold is below d; -d keeps only
// those (the complement): a LOD crossfade draws the outgoing level with d and the incoming one with
// -d, which together cover every texel once.
float ditherThreshold4x4(vec2 fragCoord)
{
	vec2 p = mod(floor(fragCoord), 4.0);
	float i = p.x + p.y * 4.0;
	// The 4x4 Bayer matrix, row by row.
	float m = 0.0;
	if      (i <  1.0) m = 0.0;  else if (i <  2.0) m = 8.0;  else if (i <  3.0) m = 2.0;  else if (i <  4.0) m = 10.0;
	else if (i <  5.0) m = 12.0; else if (i <  6.0) m = 4.0;  else if (i <  7.0) m = 14.0; else if (i <  8.0) m = 6.0;
	else if (i <  9.0) m = 3.0;  else if (i < 10.0) m = 11.0; else if (i < 11.0) m = 1.0;  else if (i < 12.0) m = 9.0;
	else if (i < 13.0) m = 15.0; else if (i < 14.0) m = 7.0;  else if (i < 15.0) m = 13.0; else m = 5.0;
	return (m + 0.5) / 16.0;
}

bool ditherDrops(float fade, vec2 fragCoord)
{
	if (fade == 0.0)
	{
		return false;
	}
	float threshold = ditherThreshold4x4(fragCoord);
	return fade > 0.0 ? threshold < fade : threshold >= -fade;
}

#endif
