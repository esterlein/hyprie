@vs vs

layout(location = 0) in vec3 pos_in;
layout(location = 1) in vec2 uv_in;


layout(set = 0, binding = 0) uniform u_camera
{
	mat4 mtx_VP;
};


layout(set = 0, binding = 1) uniform u_instance
{
    int base_instance;
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


layout(location = 0) out vec4 vtx_color;


void main()
{
	uint inst_idx = base_instance + gl_InstanceIndex;

	gl_Position = mtx_VP * trs[inst_idx].mtx_M * vec4(pos_in, 1.0);
	vtx_color   = trs[inst_idx].rgba;
}
@end


@fs fs

layout(location = 0) in  vec4 vtx_color;
layout(location = 0) out vec4 frag_color;


void main()
{
	frag_color = vtx_color;
}
@end

@program main vs fs
