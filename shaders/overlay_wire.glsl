@vs vs

layout(set = 0, binding = 0) uniform u_camera
{
	mat4 mtx_VP;
	vec2 viewport_px;
};

struct OverlayTRS
{
	mat4 mtx_M;
	vec4 rgba;
};

layout(set = 0, binding = 1, std430) readonly buffer ssbo_trs
{
	OverlayTRS trs[];
};

struct VtxNode
{
	vec4 data;
};

layout(set = 0, binding = 2, std430) readonly buffer ssbo_vtx
{
	VtxNode vtx_data[];
};

struct IdxNode
{
	uint val;
};

layout(set = 0, binding = 3, std430) readonly buffer ssbo_idx
{
	IdxNode idx_data[];
};

layout(set = 0, binding = 4) uniform u_cmd
{
	int vtx_base;
	int idx_first;
	int edges_per_instance;
	int base_trs_idx;
};

layout(location = 0) out vec4 color_fs;


void main()
{
	uint edge_idx     = gl_InstanceIndex % edges_per_instance;
	uint instance_idx = gl_InstanceIndex / edges_per_instance;

	uint raw_idx_0 = idx_data[idx_first + edge_idx * 2 + 0].val;
	uint raw_idx_1 = idx_data[idx_first + edge_idx * 2 + 1].val;

	vec3 pos_0 = vtx_data[vtx_base + raw_idx_0].data.xyz;
	vec3 pos_1 = vtx_data[vtx_base + raw_idx_1].data.xyz;

	mat4 mtx_M = trs[base_trs_idx + instance_idx].mtx_M;
	vec4 color = trs[base_trs_idx + instance_idx].rgba;

	vec4 clip_0 = mtx_VP * mtx_M * vec4(pos_0, 1.0);
	vec4 clip_1 = mtx_VP * mtx_M * vec4(pos_1, 1.0);

	vec2 ndc_0 = clip_0.xy / clip_0.w;
	vec2 ndc_1 = clip_1.xy / clip_1.w;

	vec2 screen_0 = (ndc_0 * 0.5 + 0.5) * viewport_px;
	vec2 screen_1 = (ndc_1 * 0.5 + 0.5) * viewport_px;

	vec2 dir    = normalize(screen_1 - screen_0);
	vec2 normal = vec2(-dir.y, dir.x);

	const float wire_width = 2.0;
	vec2 offset = (normal * wire_width * 0.5) / viewport_px * 2.0;

	uint corner = gl_VertexIndex;
	
	vec4 base_clip = (corner == 0 || corner == 2 || corner == 3) ? clip_0 : clip_1;
	float sign     = (corner == 0 || corner == 1 || corner == 4) ? 1.0 : -1.0;

	base_clip.xy += offset * sign * base_clip.w;

	gl_Position = base_clip;
	color_fs    = color;
}
@end


@fs fs

layout(location = 0) in  vec4 color_fs;
layout(location = 0) out vec4 frag_color;


void main()
{
	frag_color = color_fs;
}
@end

@program main vs fs
