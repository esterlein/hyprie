@vs vs
layout(location = 0) in vec3 pos_in;

layout(set = 0, binding = 0) uniform u_vs
{
	mat4 mtx_VP;
};

layout(location = 0) out vec3 local_pos_fs;

void main()
{
	local_pos_fs = pos_in;
	gl_Position = mtx_VP * vec4(pos_in, 1.0);
}
@end

@fs fs
layout(location = 0) in vec3 local_pos_fs;

layout(set = 0, binding = 0) uniform textureCube u_env_cube;
layout(set = 0, binding = 0) uniform sampler u_sampler;

layout(location = 0) out vec4 frag_color;

const float PI = 3.14159265359;

void main()
{
	vec3 normal_dir = normalize(local_pos_fs);
	vec3 irradiance_sum = vec3(0.0);

	vec3 up_dir = vec3(0.0, 1.0, 0.0);
	vec3 right_dir = normalize(cross(up_dir, normal_dir));
	up_dir = normalize(cross(normal_dir, right_dir));

	float sample_delta = 0.025;
	float sample_count = 0.0;

	for (float phi = 0.0; phi < 2.0 * PI; phi += sample_delta) {
		for (float theta = 0.0; theta < 0.5 * PI; theta += sample_delta) {
			
			vec3 tangent_offset = vec3(sin(theta) * cos(phi), sin(theta) * sin(phi), cos(theta));
			vec3 sample_dir = tangent_offset.x * right_dir + tangent_offset.y * up_dir + tangent_offset.z * normal_dir;

			irradiance_sum += texture(samplerCube(u_env_cube, u_sampler), sample_dir).rgb * cos(theta) * sin(theta);
			sample_count += 1.0;
		}
	}
	
	vec3 final_irradiance = PI * irradiance_sum * (1.0 / sample_count);
	frag_color = vec4(final_irradiance, 1.0);
}
@end

@program main vs fs
