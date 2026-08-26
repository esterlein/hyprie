@vs vs

layout(location = 0) in vec3 pos_in;
layout(location = 1) in vec4 nrm_in;
layout(location = 2) in vec4 tan_in;
layout(location = 3) in vec2 uv0_in;

layout(set = 0, binding = 0) uniform u_cam_vs
{
	mat4 mtx_VP;
	mat4 mtx_V;
	vec4 cam_pos_W;
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

layout(set = 0, binding = 12, std430) readonly buffer ssbo_trs
{
	ModelTRS trs[];
};

layout(location = 0) out vec3  pos_fs;
layout(location = 1) out vec3  nrm_fs;
layout(location = 2) out vec3  tan_fs;
layout(location = 3) out vec2  uv0_fs;
layout(location = 4) out float sgn_fs;

layout(location = 5) flat out uint mat_idx_fs;

void main()
{
	mat4 mtx_M = trs[base_inst_idx + gl_InstanceIndex].mtx_M;
	mat4 mtx_N = trs[base_inst_idx + gl_InstanceIndex].mtx_N;
	mat_idx_fs = trs[base_inst_idx + gl_InstanceIndex].mat_idx;

	mat4 mtx_MVP = mtx_VP * mtx_M;

	gl_Position = mtx_MVP * vec4(pos_in, 1.0);

	pos_fs = (mtx_M * vec4(pos_in, 1.0)).xyz;
	nrm_fs = normalize(mat3(mtx_N) * nrm_in.xyz);

	vec3 tan_world = normalize(mat3(mtx_N) * tan_in.xyz);

	tan_fs = normalize(tan_world - nrm_fs * dot(nrm_fs, tan_world));
	uv0_fs = uv0_in;
	sgn_fs = tan_in.w;
}
@end


@fs fs

layout(location = 0) in vec3  pos_fs;
layout(location = 1) in vec3  nrm_fs;
layout(location = 2) in vec3  tan_fs;
layout(location = 3) in vec2  uv0_fs;
layout(location = 4) in float sgn_fs;

layout(location = 5) flat in uint mat_idx_fs;

layout(set = 0, binding = 2) uniform u_cam_fs
{
	mat4 mtx_VP;
	mat4 mtx_V;
	vec4 cam_pos_W;
};

layout(set = 1, binding = 0)  uniform texture2DArray u_tex_arr_2048_srgb;
layout(set = 1, binding = 1)  uniform texture2DArray u_tex_arr_2048_unrm;
layout(set = 1, binding = 2)  uniform texture2DArray u_tex_arr_1024_srgb;
layout(set = 1, binding = 3)  uniform texture2DArray u_tex_arr_1024_unrm;
layout(set = 1, binding = 4)  uniform texture2DArray u_tex_arr_512_srgb;
layout(set = 1, binding = 5)  uniform texture2DArray u_tex_arr_512_unrm;
layout(set = 1, binding = 6)  uniform texture2DArray u_tex_arr_256_srgb;
layout(set = 1, binding = 7)  uniform texture2DArray u_tex_arr_256_unrm;
layout(set = 1, binding = 8)  uniform texture2DArray u_tex_arr_128_srgb;
layout(set = 1, binding = 9)  uniform texture2DArray u_tex_arr_128_unrm;
layout(set = 1, binding = 10) uniform texture2DArray u_tex_arr_64_srgb;
layout(set = 1, binding = 11) uniform texture2DArray u_tex_arr_64_unrm;

layout(set = 1, binding = 14) uniform textureCube    u_irradiance_cube;
layout(set = 1, binding = 15) uniform textureCube    u_prefilter_cube;
layout(set = 1, binding = 16) uniform texture2D      u_brdf_lut;

layout(set = 1, binding = 0) uniform sampler u_smp_linrep;
layout(set = 1, binding = 1) uniform sampler u_smp_linclamp;

struct MaterialInst
{
	vec4  alb;
	vec4  ems_mtl;
	vec4  rgh_nrm_aos_map;
	vec4  uv_scale_offset;

	uvec4 tex_info_alb;
	uvec4 tex_info_nrm;
	uvec4 tex_info_orm;
	uvec4 tex_info_ems;
};

layout(set = 0, binding = 13, std430) readonly buffer ssbo_mats
{
	MaterialInst materials[];
};

const int MAX_LIGHTS = 16;

layout(set = 0, binding = 3) uniform u_light
{
	vec4 scalar_params[MAX_LIGHTS];
	vec4 spot_params[MAX_LIGHTS];
	vec4 dir_world[MAX_LIGHTS];
	vec4 pos_world[MAX_LIGHTS];
	vec4 color_rgb[MAX_LIGHTS];
	vec3 ambient_rgb;
	int  light_count;
};

layout(location = 0) out vec4 frag_color;

const float EPSILON = 1e-6;
const float PI = 3.14159265;


vec4 fetch_texture(uvec4 tex_info, vec4 uv_transform)
{
	int array_idx = int(tex_info.x);
	float slice   = float(tex_info.y);
	vec2 uv       = uv0_fs * uv_transform.xy + uv_transform.zw;
	vec3 coord    = vec3(uv, slice);

	switch (array_idx) {
		case 0:  return texture(sampler2DArray(u_tex_arr_2048_srgb, u_smp_linrep), coord);
		case 1:  return texture(sampler2DArray(u_tex_arr_2048_unrm, u_smp_linrep), coord);
		case 2:  return texture(sampler2DArray(u_tex_arr_1024_srgb, u_smp_linrep), coord);
		case 3:  return texture(sampler2DArray(u_tex_arr_1024_unrm, u_smp_linrep), coord);
		case 4:  return texture(sampler2DArray(u_tex_arr_512_srgb,  u_smp_linrep), coord);
		case 5:  return texture(sampler2DArray(u_tex_arr_512_unrm,  u_smp_linrep), coord);
		case 6:  return texture(sampler2DArray(u_tex_arr_256_srgb,  u_smp_linrep), coord);
		case 7:  return texture(sampler2DArray(u_tex_arr_256_unrm,  u_smp_linrep), coord);
		case 8:  return texture(sampler2DArray(u_tex_arr_128_srgb,  u_smp_linrep), coord);
		case 9:  return texture(sampler2DArray(u_tex_arr_128_unrm,  u_smp_linrep), coord);
		case 10: return texture(sampler2DArray(u_tex_arr_64_srgb,   u_smp_linrep), coord);
		case 11: return texture(sampler2DArray(u_tex_arr_64_unrm,   u_smp_linrep), coord);
		default: return vec4(1.0);
	}
}

vec3 decode_normal_world(vec3 normal_map_rgb, float nrm_scale)
{
	vec3 normal_tangent_space = normal_map_rgb * 2.0 - 1.0;
	normal_tangent_space.xy  *= nrm_scale;

	vec3 surface_normal_world    = normalize(nrm_fs);
	vec3 surface_tangent_world   = normalize(tan_fs);
	vec3 surface_bitangent_world = normalize(cross(surface_normal_world, surface_tangent_world)) * sgn_fs;

	mat3 tangent_frame = mat3(surface_tangent_world, surface_bitangent_world, surface_normal_world);
	return normalize(tangent_frame * normal_tangent_space);
}

vec3 srgb_to_linear(vec3 color)
{
	return pow(color, vec3(2.2));
}

vec3 fresnel_schlick(float cos_theta, vec3 base_reflectance)
{
	return base_reflectance + (1.0 - base_reflectance) * pow(1.0 - cos_theta, 5.0);
}

float distribution_ggx(vec3 surface_normal, vec3 half_vector, float roughness)
{
	float alpha           = roughness * roughness;
	float alpha_squared   = alpha * alpha;
	float normal_dot_half = max(dot(surface_normal, half_vector), 0.0);
	float denominator     = (normal_dot_half * normal_dot_half) * (alpha_squared - 1.0) + 1.0;

	return alpha_squared / (PI * denominator * denominator);
}

float geometry_schlick_ggx(float normal_dot_view, float roughness)
{
	float roughness_plus_one = roughness + 1.0;
	float geometry_factor    = (roughness_plus_one * roughness_plus_one) / 8.0;

	return normal_dot_view / (normal_dot_view * (1.0 - geometry_factor) + geometry_factor);
}

float geometry_smith(vec3 surface_normal, vec3 view_direction, vec3 light_direction, float roughness)
{
	float normal_dot_view  = max(dot(surface_normal, view_direction),  0.0);
	float normal_dot_light = max(dot(surface_normal, light_direction), 0.0);

	return geometry_schlick_ggx(normal_dot_view, roughness) * geometry_schlick_ggx(normal_dot_light, roughness);
}

void main()
{
	MaterialInst mat = materials[mat_idx_fs];

	int map_mask = int(mat.rgh_nrm_aos_map.w);

	bool has_albedo   = (map_mask & 1) != 0;
	bool has_normal   = (map_mask & 2) != 0;
	bool has_orm      = (map_mask & 4) != 0;
	bool has_emissive = (map_mask & 8) != 0;

	vec4 alb_tint    = mat.alb;
	vec3 ems_factor  = mat.ems_mtl.xyz;
	float met_factor = mat.ems_mtl.w;
	float rgh_factor = mat.rgh_nrm_aos_map.x;
	float nrm_scale  = mat.rgh_nrm_aos_map.y;
	float ao_factor  = mat.rgh_nrm_aos_map.z;

	vec4 albedo_texel = has_albedo ? fetch_texture(mat.tex_info_alb, mat.uv_scale_offset) : vec4(1.0);
	vec3 albedo = albedo_texel.rgb * alb_tint.rgb;
	float alpha = albedo_texel.a   * alb_tint.a;

	vec3 shaded_normal_world = has_normal
		? decode_normal_world(fetch_texture(mat.tex_info_nrm, mat.uv_scale_offset).rgb, nrm_scale)
		: normalize(nrm_fs);

	vec3 orm_texel = has_orm
		? fetch_texture(mat.tex_info_orm, mat.uv_scale_offset).rgb
		: vec3(1.0, rgh_factor, met_factor);

	float ambient_occlusion = mix(1.0, orm_texel.r, ao_factor);
	float roughness = has_orm ? orm_texel.g * rgh_factor : rgh_factor;
	float metallic  = has_orm ? orm_texel.b * met_factor : met_factor;

	roughness = clamp(roughness, 0.04, 1.0);

	vec4 emissive_texel = has_emissive ? fetch_texture(mat.tex_info_ems, mat.uv_scale_offset) : vec4(0.0);
	vec3 emissive = emissive_texel.rgb * ems_factor;

	vec3 view_direction   = normalize(cam_pos_W.xyz - pos_fs);
	vec3 base_reflectance = mix(vec3(0.04), albedo, metallic);

	int light_count_clamped = min(light_count, MAX_LIGHTS);
	vec3 direct_lighting_sum = vec3(0.0);

	for (int i = 0; i < light_count_clamped; ++i) {

		int light_type  = int(scalar_params[i].x + 0.5);
		float intensity = scalar_params[i].y;
		float range     = scalar_params[i].z;

		vec3 light_color           = color_rgb[i].xyz;
		vec3 light_direction_world = dir_world[i].xyz;
		vec3 light_position_world  = pos_world[i].xyz;
		float spot_cos_inner       = spot_params[i].x;
		float spot_cos_outer       = spot_params[i].y;

		vec3 light_direction;
		float attenuation = 1.0;

		if (light_type == 0) {
			light_direction = -normalize(light_direction_world);
		}
		else {
			vec3 to_light           = light_position_world - pos_fs;
			float distance_to_light = max(length(to_light), EPSILON);
			light_direction         = to_light / distance_to_light;

			float range_safe          = max(range, EPSILON);
			float normalized_distance = distance_to_light / range_safe;
			attenuation = 1.0 / (1.0 + normalized_distance * normalized_distance);

			if (light_type == 2) {
				float cone_alignment = dot(normalize(light_direction_world), -light_direction);
				float spot_factor    = smoothstep(spot_cos_outer, spot_cos_inner, cone_alignment);
				attenuation *= spot_factor;
			}
		}

		vec3 half_vector       = normalize(view_direction + light_direction);
		float normal_dot_light = max(dot(shaded_normal_world, light_direction), 0.0);
		float normal_dot_view  = max(dot(shaded_normal_world, view_direction),  0.0);

		float microfacet_distribution = distribution_ggx(shaded_normal_world, half_vector, roughness);
		float microfacet_geometry     = geometry_smith(shaded_normal_world, view_direction, light_direction, roughness);
		vec3 fresnel_term             = fresnel_schlick(max(dot(half_vector, view_direction), 0.0), base_reflectance);

		vec3 specular_numerator    = microfacet_distribution * microfacet_geometry * fresnel_term;
		float specular_denominator = 4.0 * normal_dot_view * normal_dot_light + 0.001;
		vec3 specular              = specular_numerator / specular_denominator;

		vec3 diffuse_weight = (1.0 - fresnel_term) * (1.0 - metallic);
		vec3 irradiance = light_color * intensity * attenuation * normal_dot_light;

		direct_lighting_sum += (diffuse_weight * albedo / PI + specular) * irradiance;
	}

	/* ambient ibl */

	vec3 view_normal = normalize(shaded_normal_world);
	vec3 reflection_vector = reflect(-view_direction, view_normal);

	vec3 irradiance_sample = texture(samplerCube(u_irradiance_cube, u_smp_linclamp), view_normal).rgb;
	vec3 diffuse_ibl = irradiance_sample * albedo * (1.0 - metallic);

	const float max_reflection_lod = 4.0;
	vec3 prefiltered_color = textureLod(samplerCube(u_prefilter_cube, u_smp_linclamp), reflection_vector, roughness * max_reflection_lod).rgb;
	
	vec2 brdf_sample = texture(sampler2D(u_brdf_lut, u_smp_linclamp), vec2(max(dot(view_normal, view_direction), 0.0), roughness)).rg;
	vec3 specular_ibl = prefiltered_color * (base_reflectance * brdf_sample.x + brdf_sample.y);

	vec3 ambient_lighting = (diffuse_ibl + specular_ibl) * ambient_occlusion;
	vec3 color_out = ambient_lighting + direct_lighting_sum + emissive;

	frag_color = vec4(color_out, alpha);
}
@end

@program main vs fs
