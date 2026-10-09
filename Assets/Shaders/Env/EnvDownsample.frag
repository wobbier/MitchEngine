$input v_texcoord0

// Halves a cube face: the bilinear fetch at the destination texel centre averages 2x2 source texels.
#include "../Common.sh"
#include "EnvCommon.sh"

SAMPLERCUBE(s_envSource, 0);

void main()
{
	gl_FragColor = vec4(envCubeLod(s_envSource, currentFaceDirection(gl_FragCoord.xy), 0.0).rgb, 1.0);
}
