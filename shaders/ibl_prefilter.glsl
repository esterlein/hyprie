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

layout(set = 0, binding = 1) uniform u_fs
{
	float roughness;
};

layout(location = 0) out vec4 frag_color;

const float PI = 3.14159265359;

float radical_inverse_vdc(uint bits)
{
	bits = (bits << 16U) | (bits >> 16U);
	bits = ((bits & 0x55555555U) << 1U) | ((bits & 0xAAAAAAAAU) >> 1U);
	bits = ((bits & 0x33333333U) << 2U) | ((bits & 0xCCCCCCCCU) >> 2U);
	bits = ((bits & 0x0F0F0F0FU) << 4U) | ((bits & 0xF0F0F0F0U) >> 4U);
	bits = ((bits & 0x00FF00FFU) << 8U) | ((bits & 0xFF00FF00U) >> 8U);
	return float(bits) * 2.3283064365386963e-10;
}

vec2 hammersley(uint sample_idx, uint total_samples)
{
	return vec2(float(sample_idx) / float(total_samples), radical_inverse_vdc(sample_idx));
}

vec3 importance_sample_ggx(vec2 random_coords, vec3 normal_dir, float current_roughness)
{
	float alpha_roughness = current_roughness * current_roughness;
	float phi = 2.0 * PI * random_coords.x;
	float cos_theta = sqrt((1.0 - random_coords.y) / (1.0 + (alpha_roughness * alpha_roughness - 1.0) * random_coords.y));
	float sin_theta = sqrt(1.0 - cos_theta * cos_theta);

	vec3 tangent_space_dir;
	tangent_space_dir.x = cos(phi) * sin_theta;
	tangent_space_dir.y = sin(phi) * sin_theta;
	tangent_space_dir.z = cos_theta;

	vec3 up_dir = abs(normal_dir.z) < 0.999 ? vec3(0.0, 0.0, 1.0) : vec3(1.0, 0.0, 0.0);
	vec3 tangent_dir = normalize(cross(up_dir, normal_dir));
	vec3 bitangent_dir = cross(normal_dir, tangent_dir);

	vec3 sample_dir = tangent_dir * tangent_space_dir.x + bitangent_dir * tangent_space_dir.y + normal_dir * tangent_space_dir.z;
	return normalize(sample_dir);
}

void main()
{
	vec3 normal_dir = normalize(local_pos_fs);
	vec3 reflection_dir = normal_dir;
	vec3 view_dir = reflection_dir;

	uint max_samples = 1024U;
	float total_weight = 0.0;
	vec3 prefiltered_color = vec3(0.0);

	for (uint sample_idx = 0u; sample_idx < max_samples; ++sample_idx) {
		vec2 random_coords = hammersley(sample_idx, max_samples);
		vec3 half_dir = importance_sample_ggx(random_coords, normal_dir, roughness);
		vec3 light_dir = normalize(2.0 * dot(view_dir, half_dir) * half_dir - view_dir);

		float normal_dot_light = max(dot(normal_dir, light_dir), 0.0);
		if (normal_dot_light > 0.0) {
			prefiltered_color += texture(samplerCube(u_env_cube, u_sampler), light_dir).rgb * normal_dot_light;
			total_weight += normal_dot_light;
		}
	}
	
	prefiltered_color = prefiltered_color / total_weight;
	frag_color = vec4(prefiltered_color, 1.0);
}
@end

@program main vs fs
