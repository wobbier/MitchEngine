#include "Common.sh"

// PortalMaterial: another camera's linear HDR image, read at this pixel's screen position.
SAMPLER2D(s_texDiffuse, 0);
uniform vec4 u_screenView; // rgb tint (sRGB) shown while there is no image, w = 1 with an image

void main()
{
	vec2 uv = (gl_FragCoord.xy - u_viewRect.xy) / u_viewRect.zw;
	vec3 view = texture2D(s_texDiffuse, uv).rgb;
	gl_FragColor = vec4(mix(toLinear(u_screenView.rgb), view, u_screenView.w), 1.0);
}
