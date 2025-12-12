@vs vs

layout(location = 0) in vec3 position;

layout(set = 0, binding = 0) uniform vs_params
{
	mat4 mtx_MVP;
	mat4 mtx_MV;
};


void main()
{
	gl_Position = mtx_MVP * vec4(position, 1.0);
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

