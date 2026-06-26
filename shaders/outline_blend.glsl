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
	vec2  mask_tex_size_px;
	vec3  outline_color_rgb;
	float outline_alpha;
} outline;

layout(set = 0, binding = 2) uniform texture2D u_mask_orig_tex;
layout(set = 0, binding = 3) uniform sampler   u_mask_orig_smp;
layout(set = 0, binding = 4) uniform texture2D u_mask_dilated_tex;
layout(set = 0, binding = 5) uniform sampler   u_mask_dilated_smp;

layout(location = 0) out vec4 frag_color;


void main()
{
	vec2 mask_uv = (gl_FragCoord.xy + vec2(0.5, 0.5)) / outline.mask_tex_size_px;

	float mask_orig    = texture(sampler2D(u_mask_orig_tex,    u_mask_orig_smp),    mask_uv).r;
	float mask_dilated = texture(sampler2D(u_mask_dilated_tex, u_mask_dilated_smp), mask_uv).r;

	float edge_value = clamp(mask_dilated - mask_orig, 0.0, 1.0);

	frag_color = vec4(outline.outline_color_rgb * edge_value, outline.outline_alpha * edge_value);
}
@end

@program main vs fs

