@vs vs

void main()
{
	vec2 corner_bit   = vec2((gl_VertexIndex << 1) & 2, gl_VertexIndex & 2);
	vec2 position_ndc = corner_bit * 2.0 - 1.0;
	gl_Position       = vec4(position_ndc, 0.0, 1.0);
}
@end


@fs fs

layout(set = 0, binding = 0) uniform fs_params
{
	vec2 mask_tex_size_px;
	int  radius_px;
};

layout(set = 0, binding = 2) uniform texture2D u_mask_tex;
layout(set = 0, binding = 3) uniform sampler   u_mask_smp;

layout(location = 0) out vec4 frag_color;


void main()
{
	vec2 texel_size = 1.0 / mask_tex_size_px;
	vec2 mask_uv    = (gl_FragCoord.xy + vec2(0.5, 0.5)) / mask_tex_size_px;

	int radius      = max(radius_px, 0);
	float max_value = 0.0;

	for (int dy = -radius; dy <= radius; ++dy) {
		for (int dx = -radius; dx <= radius; ++dx) {

			vec2 offset     = vec2(float(dx), float(dy)) * texel_size;
			float smp_value = texture(sampler2D(u_mask_tex, u_mask_smp), mask_uv + offset).r;
			max_value       = max(max_value, smp_value);
		}
	}

	frag_color = vec4(max_value, 0.0, 0.0, 1.0);
}
@end

@program main vs fs

