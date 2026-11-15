$input v_texcoord0

// Composites froxel volumetric fog onto the opaque HDR scene (blend: src + dst * src.a), the
// compute path's twin of Fog.frag: the integrated grid (FogFroxelIntegrate.comp) is read at this
// pixel's screen position and depth, one trilinear lookup, and the fog beyond the grid is added
// analytically as the march does.
#include "../Common.sh"
#include "../Lighting.sh"
#include "../Fog.sh"
#include "FogFroxel.sh"

SAMPLER3D(s_froxelVolume, 0);   // integrated: rgb in-scattering, a transmittance up to each cell's far boundary
SAMPLER2D(s_input1, 1);         // camera depth

void main()
{
	vec2 uv = v_texcoord0;
	vec2 fullTexel = vec2(1.0 / u_fogScreen.x, 1.0 / u_fogScreen.y);
	float viewZ = fogViewDepth(texture2D(s_input1, (floor(uv / fullTexel) + 0.5) * fullTexel).r);
	vec3 dir;
	float distance;
	float viewZPerMetre;
	float originViewZ;
	vec3 origin = fogRay(uv, viewZ, dir, distance, viewZPerMetre, originViewZ);

	// Cell k holds the fog up to cell coordinate k + 1; in front of the first boundary the first
	// cell's fog fades in from the fog's start.
	vec4 volume = vec4(0.0, 0.0, 0.0, 1.0);
	float s = froxelCellCoord(viewZ);
	if (s > 0.0)
	{
		if (s < 1.0)
		{
			volume = mix(volume, texture3DLod(s_froxelVolume, vec3(uv, 0.5 / u_froxelGrid.z), 0.0), s);
		}
		else
		{
			volume = texture3DLod(s_froxelVolume, vec3(uv, (s - 0.5) / u_froxelGrid.z), 0.0);
		}
	}
	float gridEnd = min(distance, u_froxelDepth.y / max(viewZPerMetre, 1e-4));
	vec4 rest = fogAnalytic(origin, dir, gridEnd, distance, fogDirectionalRadiance(dir, origin + dir * ((gridEnd + distance) * 0.5)));
	gl_FragColor = fogClampOpacity(vec4(volume.rgb + volume.a * rest.rgb, volume.a * rest.a));
}
