#ifndef SKINNING_SH
#define SKINNING_SH

// GPU skinning: up to four influences per vertex (a_indices / a_weight, vertex stream 1) into a
// palette of mesh-space bone matrices set per draw. Vertices without weights stay rigid.
#define MAX_BONES 128
uniform mat4 u_bones[MAX_BONES];

// indices arrive normalized (bone / 255).
mat4 skinMatrix(vec4 indices, vec4 weights)
{
	indices = floor(indices * 255.0 + 0.5);
	float total = weights.x + weights.y + weights.z + weights.w;
	if (total < 0.0001)
	{
		return mtxFromCols(vec4(1.0, 0.0, 0.0, 0.0), vec4(0.0, 1.0, 0.0, 0.0), vec4(0.0, 0.0, 1.0, 0.0), vec4(0.0, 0.0, 0.0, 1.0));
	}
	return u_bones[int(indices.x)] * weights.x
		+ u_bones[int(indices.y)] * weights.y
		+ u_bones[int(indices.z)] * weights.z
		+ u_bones[int(indices.w)] * weights.w;
}

#endif // SKINNING_SH
