@vs vs

void main()
{
	vec2 corner_bit   = vec2((gl_VertexIndex << 1) & 2, gl_VertexIndex & 2);
	vec2 position_ndc = corner_bit * 2.0 - 1.0;

	gl_Position = vec4(position_ndc, 0.0, 1.0);
}
@end


@fs fs

layout(set = 0, binding = 0) uniform u_camera
{
	mat4  mtx_VP_inv;
	mat4  mtx_VP;
	vec2  fb_size_px;
} camera;

layout(set = 0, binding = 1) uniform u_fx_payload
{
	vec4  minor_rgba;
	vec4  major_rgba;
	vec2  minor_vis_range_px;
	vec2  major_vis_range_px;

	float line_width_px;
	float cell_size;
	float y_plane;
	float major_step_cells;

	vec2  cam_offset_xz;
	vec2  camera_world_xz;
	float camera_y;
} grid;

layout(location = 0) out vec4 frag_color;

const float k_epsilon = 1e-6;

void main()
{
	vec2 uv_screen = gl_FragCoord.xy / camera.fb_size_px;
	vec2 ndc       = uv_screen * 2.0 - 1.0;

	vec4 clip_near  = vec4(ndc, -1.0, 1.0);
	vec4 clip_far   = vec4(ndc,  1.0, 1.0);
	vec4 world_near = camera.mtx_VP_inv * clip_near;
	vec4 world_far  = camera.mtx_VP_inv * clip_far;
	
	vec3 ray_origin = world_near.xyz / world_near.w;
	vec3 ray_dir    = (world_far.xyz / world_far.w) - ray_origin;

	float denom = ray_dir.y;
	if (abs(denom) < k_epsilon) discard;

	float t = (grid.y_plane - ray_origin.y) / denom;
	if (t < 0.0) discard;

	vec3 world_pos = ray_origin + ray_dir * t;

	vec2 local_xz = world_pos.xz - grid.camera_world_xz;
	vec2 coord    = (local_xz + grid.cam_offset_xz) / grid.cell_size;

	vec2 ddx   = dFdx(coord);
	vec2 ddy   = dFdy(coord);
	vec2 deriv = max(vec2(length(vec2(ddx.x, ddy.x)), length(vec2(ddx.y, ddy.y))), vec2(1e-5));

	float half_width = grid.line_width_px * 0.5;
	float major_step = grid.major_step_cells;

	vec2 dist_minor  = abs(fract(coord - 0.5) - 0.5);
	vec2 width_minor = half_width * deriv;
	vec2 aa_minor    = deriv;
	
	vec2 line_minor_axes = smoothstep(width_minor + aa_minor, width_minor - aa_minor, dist_minor);
	float line_minor     = max(line_minor_axes.x, line_minor_axes.y);

	vec2 coord_major = coord / major_step;
	vec2 ddx_maj     = ddx / major_step;
	vec2 ddy_maj     = ddy / major_step;
	vec2 deriv_major = vec2(length(vec2(ddx_maj.x, ddy_maj.x)), length(vec2(ddx_maj.y, ddy_maj.y)));
	
	vec2 dist_major  = abs(fract(coord_major - 0.5) - 0.5);
	vec2 width_major = half_width * deriv_major;
	vec2 aa_major    = deriv_major;
	
	vec2 line_major_axes = smoothstep(width_major + aa_major, width_major - aa_major, dist_major);
	float line_major     = max(line_major_axes.x, line_major_axes.y);

	float px_per_cell = 1.0 / max(length(deriv), k_epsilon);
	
	float minor_vis =
		clamp((px_per_cell - grid.minor_vis_range_px.x) /
		max(grid.minor_vis_range_px.y - grid.minor_vis_range_px.x, k_epsilon), 0.0, 1.0);

	float major_vis =
		clamp((px_per_cell * major_step - grid.major_vis_range_px.x) /
		max(grid.major_vis_range_px.y - grid.major_vis_range_px.x, k_epsilon), 0.0, 1.0);

	float alpha_minor = line_minor * minor_vis;
	float alpha_major = line_major * major_vis;
	float alpha       = max(alpha_minor, alpha_major);

	if (alpha < 0.01) discard;

	vec3 color = mix(grid.minor_rgba.rgb, grid.major_rgba.rgb, step(alpha_minor, alpha_major));

	vec4 clip    = camera.mtx_VP * vec4(world_pos, 1.0);
	gl_FragDepth = (clip.z / clip.w) * 0.5 + 0.5;

	frag_color = vec4(color, alpha);
}
@end

@program main vs fs
