@vs vs

layout(location = 0) in vec3 position;

layout(set = 0, binding = 0) uniform vs_params
{
	mat4 mtx_VP;
	mat4 mtx_M;
	vec4 color_rgba;
};

layout(location = 0) out vec4 vs_color;


void main()
{
	gl_Position = mtx_VP * mtx_M * vec4(position, 1.0);
	vs_color    = color_rgba;
}
@end


@fs fs

layout(location = 0) in vec4 vs_color;

layout(location = 0) out vec4 frag_color;


void main()
{
	frag_color = vs_color;
}
@end

@program main vs fs

