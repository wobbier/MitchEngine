$input a_position, a_texcoord0, i_data0, i_data1, i_data2, i_data3
$output v_texcoord0, v_color0, v_viewPos

// Instanced particle quads. Instance: i_data0 position + size, i_data1 colour, i_data2 rotation +
// flipbook frame, i_data3 velocity. Camera axes come from u_invView (camera to world), which holds
// on every backend.
#include "Common.sh"

uniform vec4 u_particleParams;   // x flipbook columns, y rows, z softness, w alignment (0 billboard, 1 stretched, 2 horizontal)
uniform vec4 u_particleParams2;  // x stretch factor

void main()
{
	vec3 center = i_data0.xyz;
	float size = i_data0.w;
	vec2 corner = a_position.xy;
	float c = cos(i_data2.x);
	float s = sin(i_data2.x);
	vec2 rotated = vec2(c * corner.x - s * corner.y, s * corner.x + c * corner.y);

	vec3 right = mul(u_invView, vec4(1.0, 0.0, 0.0, 0.0)).xyz;
	vec3 up = mul(u_invView, vec4(0.0, 1.0, 0.0, 0.0)).xyz;
	vec3 worldPos = center + (right * rotated.x + up * rotated.y) * size;
	vec3 velocity = i_data3.xyz;
	float speedSq = dot(velocity, velocity);
	if (u_particleParams.w > 1.5)
	{
		worldPos = center + vec3(rotated.x, 0.0, rotated.y) * size;
	}
	else if (u_particleParams.w > 0.5 && speedSq > 1e-6)
	{
		// Long axis along the velocity, wide axis facing the camera; the head sits on the particle.
		vec3 cameraPos = mul(u_invView, vec4(0.0, 0.0, 0.0, 1.0)).xyz;
		vec3 axis = velocity * inversesqrt(speedSq);
		vec3 side = cross(axis, normalize(cameraPos - center));
		side = dot(side, side) > 1e-6 ? normalize(side) : right;
		float len = size + sqrt(speedSq) * u_particleParams2.x;
		worldPos = center + side * (corner.x * size) + axis * ((corner.y - 0.5) * len);
	}

	gl_Position = mul(u_viewProj, vec4(worldPos, 1.0));
	v_viewPos = mul(u_view, vec4(worldPos, 1.0));

	vec2 grid = max(u_particleParams.xy, vec2_splat(1.0));
	float frame = mod(floor(i_data2.y), grid.x * grid.y);
	vec2 cell = vec2(mod(frame, grid.x), floor(frame / grid.x));
	v_texcoord0 = (cell + a_texcoord0) / grid;
	v_color0 = i_data1;
}
