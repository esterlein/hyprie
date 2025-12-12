@vs vs

layout(location = 0) in vec3 position_in;
layout(location = 1) in vec2 uv_in;

layout(set = 0, binding = 0) uniform vs_view_params
{
	mat4 mtx_VP;
	mat4 mtx_M;
} view;

layout(location = 0) out vec2 uv;

void main()
{
	uv = uv_in;

	gl_Position = view.mtx_VP * view.mtx_M * vec4(position_in, 1.0);
}
@end


@fs fs

layout(set = 0, binding = 1) uniform texture2D u_palette_tex;
layout(set = 0, binding = 2) uniform sampler   u_palette_smp;

layout(set = 0, binding = 3) uniform utexture2D u_tilemap_tex;

@sampler_type u_tilemap_smp nonfiltering
layout(set = 0, binding = 4) uniform sampler u_tilemap_smp;


layout(set = 0, binding = 5) uniform fs_tile_params
{
	vec4 fill;
	vec4 border_color;
	int  chunk_size;
} tile;

layout(location = 0) in vec2 uv;

layout(location = 0) out vec4 frag_color;


void main()
{
	vec2  tile_uv = uv * float(tile.chunk_size);
	ivec2 tile_xy = ivec2(floor(tile_uv));
	vec2  cell_uv = fract(tile_uv);

	uint tile_type = texelFetch(usampler2D(u_tilemap_tex, u_tilemap_smp), tile_xy, 0).r;

	if (tile_type == 0U) {
		discard;
	}

	float edge = min(
		min(cell_uv.x, 1.0 - cell_uv.x),
		min(cell_uv.y, 1.0 - cell_uv.y)
	);

	float border_width_uv = tile.fill.y;

	if (edge >= border_width_uv) {

		uint palette_index = (tile_type > 255U) ? 255U : tile_type;

		float palette_sample_x = (float(palette_index) + 0.5) / 256.0;
		vec4 fill_rgba = texture(
			sampler2D(u_palette_tex, u_palette_smp),
			vec2(palette_sample_x, 0.5)
		);

		frag_color = fill_rgba;
		return;
	}

	bool draw_border = false;

	const ivec2 left_xy   = tile_xy + ivec2(-1,  0);
	const ivec2 right_xy  = tile_xy + ivec2( 1,  0);
	const ivec2 bottom_xy = tile_xy + ivec2( 0, -1);
	const ivec2 top_xy    = tile_xy + ivec2( 0,  1);

	if (cell_uv.x <= border_width_uv) {
		draw_border = true;
	}
	else if (cell_uv.y <= border_width_uv) {
		draw_border = true;
	}
	else if (cell_uv.x >= 1.0 - border_width_uv) {

		if (tile_xy.x == tile.chunk_size - 1) {
			draw_border = true;
		}
		else {
			uint right_type = texelFetch(usampler2D(u_tilemap_tex, u_tilemap_smp), right_xy, 0).r;

			draw_border = (right_type == 0U);
		}
	}
	else if (cell_uv.y >= 1.0 - border_width_uv) {

		if (tile_xy.y == tile.chunk_size - 1) {
			draw_border = true;
		}
		else {
			uint top_type = texelFetch(usampler2D(u_tilemap_tex, u_tilemap_smp), top_xy, 0).r;

			draw_border = (top_type == 0U);
		}
	}

	if (draw_border) {
		frag_color = tile.border_color;
		return;
	}

	uint palette_index = (tile_type > 255U) ? 255U : tile_type;

	float palette_sample_x = (float(palette_index) + 0.5) / 256.0;
	vec4 fill_rgba = texture(
		sampler2D(u_palette_tex, u_palette_smp),
		vec2(palette_sample_x, 0.5)
	);

	frag_color = fill_rgba;
}


@end

@program main vs fs

