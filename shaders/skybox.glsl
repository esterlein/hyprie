@vs vs
layout(location = 0) in vec3 pos_in;

layout(set = 0, binding = 0) uniform u_camera
{
	mat4 mtx_VP;
};

layout(location = 0) out vec3 pos_fs;

void main()
{
	pos_fs = pos_in;
	vec4 pos_clip = mtx_VP * vec4(pos_in, 1.0);
	gl_Position = pos_clip.xyww;
}
@end

@fs fs
layout(location = 0) in vec3 pos_fs;

layout(set = 1, binding = 0) uniform textureCube u_skybox_cube;
layout(set = 1, binding = 0) uniform sampler     u_smp_linear;

layout(location = 0) out vec4 frag_color;

void main()
{
	vec3 env_color = texture(samplerCube(u_skybox_cube, u_smp_linear), normalize(pos_fs)).rgb;
	frag_color = vec4(env_color, 1.0);
}
@end

@program main vs fs
