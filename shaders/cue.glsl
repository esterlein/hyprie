@vs vs

layout(location = 0) in vec3 pos_in;
layout(location = 1) in vec2 uv_in;


layout(set = 0, binding = 0) uniform u_camera
{
	mat4 mtx_VP;
};


struct CueTRS
{
	mat4 mtx_M;
};

layout(set = 0, binding = 1, std430) readonly buffer ssbo_trs
{
	CueTRS trs[];
};


layout(location = 0) out vec2 uv_fs;


void main()
{
	uv_fs = uv_in;

	mat4 mtx_M = trs[gl_InstanceIndex].mtx_M;

	gl_Position = mtx_VP * mtx_M * vec4(pos_in, 1.0);
}
@end


@fs fs

layout(location = 0) in vec2 uv_fs;


layout(set = 0, binding = 2) uniform texture2D  u_tex_palette;
layout(set = 0, binding = 3) uniform sampler    u_smp_palette;

layout(set = 0, binding = 4) uniform utexture2D u_tex_tilemap;
@sampler_type u_smp_tilemap nonfiltering
layout(set = 0, binding = 5) uniform sampler    u_smp_tilemap;


layout(set = 0, binding = 6) uniform u_cue_params
{
	int mask;
	int palette;
	int tilemap;
} u_cue;


layout(location = 0) out vec4 frag_color;


void main()
{
	const float wire_width = 2.0;
	const int chunk_size   = 32;

	vec2 cell_uv;
	vec2 continuous_uv;
	vec4 base_color;
	bool draw_fill;
	
	uint cue_type = uint(u_cue.mask) & 0xFFU;

	switch (cue_type) {
		case 1U: {
			continuous_uv = uv_fs * float(chunk_size);
			ivec2 tile_xy = ivec2(floor(continuous_uv));
			cell_uv = fract(continuous_uv);

			uint tile_type = texelFetch(
				usampler2D(u_tex_tilemap, u_smp_tilemap),
				ivec2(tile_xy.x, tile_xy.y + int(u_cue.tilemap) * chunk_size),
				0
			).r;

			if (tile_type == 0U) {
				discard;
			}

			uint palette_index     = (tile_type > 255U) ? 255U : tile_type;
			float palette_sample_x = (float(palette_index) + 0.5) / 256.0;
			float palette_sample_y = (float(u_cue.palette) + 0.5) / 256.0;

			base_color = texture(
				sampler2D(u_tex_palette, u_smp_palette),
				vec2(palette_sample_x, palette_sample_y)
			);
			draw_fill = true;
			break;
		}
		case 0U:
		default: {
			continuous_uv = uv_fs;
			cell_uv       = fract(uv_fs);
			base_color    = vec4(0.0, 0.7, 1.0, 1.0);
			draw_fill     = false;

			break;
		}
	}

	vec2 dist_to_edge = min(cell_uv, 1.0 - cell_uv);

	vec2 dx = dFdx(continuous_uv);
	vec2 dy = dFdy(continuous_uv);

	vec2 uv_per_px = vec2(
		length(vec2(dx.x, dy.x)),
		length(vec2(dx.y, dy.y))
	);

	vec2 edge_mask_2d = smoothstep(
		(uv_per_px * wire_width) * 0.5,
		(uv_per_px * wire_width) * 1.5,
		dist_to_edge
	);

	float inside_mask  = min(edge_mask_2d.x, edge_mask_2d.y);
	float border_alpha = 1.0 - inside_mask;

	if (border_alpha > 0.0) {
		frag_color = vec4(base_color.rgb, base_color.a * border_alpha);
	}
	else {
		if (draw_fill) {
			frag_color = base_color;
		}
		else {
			discard;
		}
	}
}
@end

@program main vs fs
