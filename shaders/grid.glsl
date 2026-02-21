@vs vs

void main()
{
	vec2 corner_bit   = vec2((gl_VertexIndex << 1) & 2, gl_VertexIndex & 2);
	vec2 position_ndc = corner_bit * 2.0 - 1.0;

	gl_Position = vec4(position_ndc, 0.0, 1.0);
}
@end


@fs fs

layout(set = 0, binding = 0) uniform fs_camera_params
{
	mat4  mtx_VP_inv;
	mat4  mtx_VP;
	vec2  framebuffer_size_px;
	float grid_plane_y;
	float cell_size_world;
} camera;

layout(set = 0, binding = 1) uniform fs_params
{
	float line_width_px;
	int   major_step_cells;
	vec2  minor_visibility_range_px;
	vec2  major_visibility_range_px;
	vec4  minor_color_rgba;
	vec4  major_color_rgba;
} grid;

layout(location = 0) out vec4 frag_color;


const float k_epsilon = 1e-6;


void main()
{
	vec2 uv_screen = gl_FragCoord.xy / camera.framebuffer_size_px;
	vec2 ndc       = uv_screen * 2.0 - 1.0;

	vec4 clip_near = vec4(ndc, -1.0, 1.0);
	vec4 clip_far  = vec4(ndc,  1.0, 1.0);

	vec4 world_near = camera.mtx_VP_inv * clip_near;
	vec4 world_far  = camera.mtx_VP_inv * clip_far;
	world_near.xyz /= world_near.w;
	world_far.xyz  /= world_far.w;

	vec3 ray_origin = world_near.xyz;
	vec3 ray_dir    = world_far.xyz - world_near.xyz;

	float denom = ray_dir.y;
	if (abs(denom) < k_epsilon)
		discard;

	float ray_hit_factor = (camera.grid_plane_y - ray_origin.y) / denom;
	if (ray_hit_factor < 0.0)
		discard;

	vec3 grid_hit_world = ray_origin + ray_dir * ray_hit_factor;

	vec2 grid_coord = grid_hit_world.xz / camera.cell_size_world;

	float grid_x = grid_coord.x;
	float grid_y = grid_coord.y;

	vec2 d_grid_x = vec2(dFdx(grid_x), dFdy(grid_x));
	vec2 d_grid_y = vec2(dFdx(grid_y), dFdy(grid_y));

	float cells_per_px_x = max(length(d_grid_x), k_epsilon);
	float cells_per_px_y = max(length(d_grid_y), k_epsilon);

	float px_per_cell_x = 1.0 / cells_per_px_x;
	float px_per_cell_y = 1.0 / cells_per_px_y;

	float fx = fract(grid_x);
	float fy = fract(grid_y);

	float dist_cell_x = min(fx, 1.0 - fx);
	float dist_cell_y = min(fy, 1.0 - fy);

	float minor_half_width_px = grid.line_width_px * 0.5;

	float minor_half_width_cells_x = minor_half_width_px * cells_per_px_x;
	float minor_half_width_cells_y = minor_half_width_px * cells_per_px_y;

	float minor_aa_cells_x = fwidth(dist_cell_x);
	float minor_aa_cells_y = fwidth(dist_cell_y);

	float mask_x_minor =
		1.0 - smoothstep(
			minor_half_width_cells_x - minor_aa_cells_x,
			minor_half_width_cells_x + minor_aa_cells_x,
			dist_cell_x
		);
	float mask_y_minor =
		1.0 - smoothstep(
			minor_half_width_cells_y - minor_aa_cells_y,
			minor_half_width_cells_y + minor_aa_cells_y,
			dist_cell_y
		);
	float mask_minor = max(mask_x_minor, mask_y_minor);

	float major_step = float(max(grid.major_step_cells, 1));

	float grid_x_major = grid_x / major_step;
	float grid_y_major = grid_y / major_step;

	float fx_major = fract(grid_x_major);
	float fy_major = fract(grid_y_major);

	float dist_cell_major_x = min(fx_major, 1.0 - fx_major);
	float dist_cell_major_y = min(fy_major, 1.0 - fy_major);

	float cells_per_px_x_major = cells_per_px_x / major_step;
	float cells_per_px_y_major = cells_per_px_y / major_step;

	float major_half_width_px = grid.line_width_px;

	float major_half_width_cells_x = major_half_width_px * cells_per_px_x_major;
	float major_half_width_cells_y = major_half_width_px * cells_per_px_y_major;

	float major_aa_cells_x = fwidth(dist_cell_major_x);
	float major_aa_cells_y = fwidth(dist_cell_major_y);

	float mask_x_major =
		1.0 - smoothstep(
			major_half_width_cells_x - major_aa_cells_x,
			major_half_width_cells_x + major_aa_cells_x,
			dist_cell_major_x
		);
	float mask_y_major =
		1.0 - smoothstep(
			major_half_width_cells_y - major_aa_cells_y,
			major_half_width_cells_y + major_aa_cells_y,
			dist_cell_major_y
		);
	float mask_major = max(mask_x_major, mask_y_major);

	float px_per_cell_minor = min(px_per_cell_x, px_per_cell_y);
	float px_per_cell_major = major_step * px_per_cell_minor;

	float minor_vis =
		clamp(
			(px_per_cell_minor - grid.minor_visibility_range_px.x) /
			max(grid.minor_visibility_range_px.y - grid.minor_visibility_range_px.x, k_epsilon),
			0.0,
			1.0
		);
	float major_vis =
		clamp(
			(px_per_cell_major - grid.major_visibility_range_px.x) /
			max(grid.major_visibility_range_px.y - grid.major_visibility_range_px.x, k_epsilon),
			0.0,
			1.0
		);

	float alpha_minor = mask_minor * minor_vis;
	float alpha_major = mask_major * major_vis;

	float alpha   = max(alpha_minor, alpha_major);
	vec3 line_rgb = mix(grid.minor_color_rgba.rgb, grid.major_color_rgba.rgb, step(alpha_minor, alpha_major));

	vec4 clip = camera.mtx_VP * vec4(grid_hit_world, 1.0);

	float ndc_z  = clip.z / clip.w;
	gl_FragDepth = ndc_z * 0.5 + 0.5;

	frag_color = vec4(line_rgb, alpha);
}
@end

@program main vs fs

