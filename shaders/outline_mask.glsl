@vs vs

layout(location = 0) in vec3 pos_in;


layout(set = 0, binding = 0) uniform u_camera
{
	mat4 mtx_V;
	mat4 mtx_VP;
};


layout(set = 0, binding = 1) uniform u_inst
{
	int base_inst_idx;
};


struct ModelTRS
{
	mat4 mtx_M;
	mat4 mtx_N;
	uint mat_idx;
};

layout(set = 0, binding = 2, std430) readonly buffer ssbo_trs
{
	ModelTRS trs[];
};


void main()
{
	gl_Position = mtx_VP * trs[base_inst_idx].mtx_M * vec4(pos_in, 1.0);
}
@end


@fs fs

layout(location = 0) out vec4 frag_color;


void main()
{
	frag_color = vec4(1.0);
}
@end

@program main vs fs
