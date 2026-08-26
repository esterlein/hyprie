@vs vs
layout(location = 0) in vec3 pos_in;
layout(location = 1) in vec2 uv_in;

layout(location = 0) out vec2 uv_fs;


void main()
{
	uv_fs = uv_in;
	gl_Position = vec4(pos_in, 1.0);
}
@end


@fs fs
layout(location = 0) in vec2 uv_fs;

layout(location = 0) out vec2 frag_color;

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

float geometry_schlick_ggx(float normal_dot_view, float current_roughness)
{
	float geom_k = (current_roughness * current_roughness) / 2.0;
	return normal_dot_view / (normal_dot_view * (1.0 - geom_k) + geom_k);
}

float geometry_smith(vec3 normal_dir, vec3 view_dir, vec3 light_dir, float current_roughness)
{
	float normal_dot_view = max(dot(normal_dir, view_dir), 0.0);
	float normal_dot_light = max(dot(normal_dir, light_dir), 0.0);
	return geometry_schlick_ggx(normal_dot_view, current_roughness) * geometry_schlick_ggx(normal_dot_light, current_roughness);
}

vec2 integrate_brdf(float normal_dot_view, float current_roughness)
{
	vec3 view_dir = vec3(sqrt(1.0 - normal_dot_view * normal_dot_view), 0.0, normal_dot_view);
	vec3 normal_dir = vec3(0.0, 0.0, 1.0);
	
	float scale_out = 0.0;
	float bias_out = 0.0;

	uint max_samples = 1024U;
	for (uint sample_idx = 0u; sample_idx < max_samples; ++sample_idx) {
		
		vec2 random_coords = hammersley(sample_idx, max_samples);
		vec3 half_dir = importance_sample_ggx(random_coords, normal_dir, current_roughness);
		vec3 light_dir = normalize(2.0 * dot(view_dir, half_dir) * half_dir - view_dir);

		float normal_dot_light = max(light_dir.z, 0.0);
		float normal_dot_half = max(half_dir.z, 0.0);
		float view_dot_half = max(dot(view_dir, half_dir), 0.0);

		if (normal_dot_light > 0.0) {
			float geom_term = geometry_smith(normal_dir, view_dir, light_dir, current_roughness);
			float geom_vis = (geom_term * view_dot_half) / (normal_dot_half * normal_dot_view);
			float fresnel_val = pow(1.0 - view_dot_half, 5.0);

			scale_out += (1.0 - fresnel_val) * geom_vis;
			bias_out += fresnel_val * geom_vis;
		}
	}
	return vec2(scale_out, bias_out) / float(max_samples);
}

void main()
{
	vec2 integrated_brdf = integrate_brdf(uv_fs.x, uv_fs.y);
	frag_color = integrated_brdf;
}
@end

@program main vs fs
