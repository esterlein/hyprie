@vs vs

layout(location = 0) in vec3 position;
layout(location = 1) in vec3 normal;
layout(location = 2) in vec4 tangent;
layout(location = 3) in vec2 uv_0_in;
layout(location = 4) in vec2 uv_1_in;
layout(location = 5) in vec4 color;
layout(location = 6) in uint extra;

layout(set = 0, binding = 0) uniform vs_params
{
	mat4 mtx_MVP;
	mat4 mtx_MV;
};

layout(location = 0) out vec2  uv_0;
layout(location = 1) out vec2  uv_1;
layout(location = 2) out vec3  normal_view;
layout(location = 3) out vec3  tangent_view;
layout(location = 4) out float tangent_sign;
layout(location = 5) out vec3  position_view;


void main()
{
	gl_Position = mtx_MVP * vec4(position, 1.0);

	uv_0 = uv_0_in;
	uv_1 = uv_1_in;

	mat3 mtx_normal = transpose(inverse(mat3(mtx_MV)));
	vec3 nrm_view   = normalize(mtx_normal * normal);
	vec3 tan_view   = normalize(mtx_normal * tangent.xyz);
	tan_view        = normalize(tan_view - nrm_view * dot(nrm_view, tan_view));

	normal_view   = nrm_view;
	tangent_view  = tan_view;
	tangent_sign  = tangent.w;
	position_view = (mtx_MV * vec4(position, 1.0)).xyz;
}
@end


@fs fs

layout(location = 0) in vec2  uv_0;
layout(location = 1) in vec2  uv_1;
layout(location = 2) in vec3  normal_view;
layout(location = 3) in vec3  tangent_view;
layout(location = 4) in float tangent_sign;
layout(location = 5) in vec3  position_view;

layout(set = 0, binding = 2) uniform texture2D u_tex_albedo_tex;
layout(set = 0, binding = 3) uniform sampler   u_tex_albedo_smp;
layout(set = 0, binding = 4) uniform texture2D u_tex_normal_tex;
layout(set = 0, binding = 5) uniform sampler   u_tex_normal_smp;
layout(set = 0, binding = 6) uniform texture2D u_tex_orm_tex;
layout(set = 0, binding = 7) uniform sampler   u_tex_orm_smp;
layout(set = 0, binding = 8) uniform texture2D u_tex_emissive_tex;
layout(set = 0, binding = 9) uniform sampler   u_tex_emissive_smp;

layout(set = 0, binding = 1) uniform fs_pbr_params
{
	vec4  albedo_tint;
	vec3  emissive_factor;
	float metallic_factor;
	float roughness_factor;
	float normal_scale;
	float ao_strength;
	int   map_mask;
	vec2  uv_scale;
	vec2  uv_offset;
	int   uv_index_albedo;
	int   uv_index_normal;
	int   uv_index_orm;
	int   uv_index_emissive;
};

const int MAX_LIGHTS = 16;

layout(set = 1, binding = 2) uniform fs_light_params
{
	int  light_count;
	vec3 ambient_rgb;
	vec4 light_scalar_params[MAX_LIGHTS];
	vec4 light_color_rgb[MAX_LIGHTS];
	vec4 light_dir_view[MAX_LIGHTS];
	vec4 light_pos_view[MAX_LIGHTS];
	vec4 light_spot_params[MAX_LIGHTS];
};

layout(location = 0) out vec4 frag_color;


const float epsilon = 1e-6;
const float pi = 3.14159265;


vec2 select_uv(int index)
{
	vec2 base_uv = (index == 1) ? uv_1 : uv_0;
	return base_uv * uv_scale + uv_offset;
}

vec3 decode_normal_view(vec3 normal_map_rgb)
{
	vec3 normal_tangent_space = normal_map_rgb * 2.0 - 1.0;
	normal_tangent_space.xy  *= normal_scale;

	vec3 surface_normal_view    = normalize(normal_view);
	vec3 surface_tangent_view   = normalize(tangent_view - surface_normal_view * dot(surface_normal_view, tangent_view));
	vec3 surface_bitangent_view = normalize(cross(surface_normal_view, surface_tangent_view)) * tangent_sign;

	mat3 tangent_frame = mat3(surface_tangent_view, surface_bitangent_view, surface_normal_view);
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

	return alpha_squared / (pi * denominator * denominator);
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
	bool has_albedo   = (map_mask & 1) != 0;
	bool has_normal   = (map_mask & 2) != 0;
	bool has_orm      = (map_mask & 4) != 0;
	bool has_emissive = (map_mask & 8) != 0;

	vec4 albedo_texel = has_albedo
		? texture(sampler2D(u_tex_albedo_tex, u_tex_albedo_smp), select_uv(uv_index_albedo))
		: vec4(1.0);

	vec3 albedo = albedo_texel.rgb * albedo_tint.rgb;
	float alpha = albedo_texel.a   * albedo_tint.a;

	vec3 shaded_normal_view = has_normal
		? decode_normal_view(texture(sampler2D(u_tex_normal_tex, u_tex_normal_smp), select_uv(uv_index_normal)).rgb)
		: normalize(normal_view);

	vec3 orm_texel = has_orm
		? texture(sampler2D(u_tex_orm_tex, u_tex_orm_smp), select_uv(uv_index_orm)).rgb
		: vec3(1.0, roughness_factor, metallic_factor);

	float ambient_occlusion = mix(1.0, orm_texel.r, ao_strength);
	float roughness = has_orm ? orm_texel.g * roughness_factor : roughness_factor;
	float metallic  = has_orm ? orm_texel.b * metallic_factor : metallic_factor;

	roughness = clamp(roughness, 0.04, 1.0);

	vec4 emissive_texel = has_emissive
		? texture(sampler2D(u_tex_emissive_tex, u_tex_emissive_smp), select_uv(uv_index_emissive))
		: vec4(0.0);

	vec3 emissive = srgb_to_linear(emissive_texel.rgb) * emissive_factor;

	vec3 view_direction   = normalize(-position_view);
	vec3 base_reflectance = mix(vec3(0.04), albedo, metallic);

	int light_count_clamped = light_count;
	if (light_count_clamped > MAX_LIGHTS) {
		light_count_clamped = MAX_LIGHTS;
	}

	vec3 direct_lighting_sum = vec3(0.0);

	for (int i = 0; i < light_count_clamped; ++i) {

		int light_type  = int(light_scalar_params[i].x + 0.5);
		float intensity = light_scalar_params[i].y;
		float range     = light_scalar_params[i].z;

		vec3 light_color          = light_color_rgb[i].xyz;
		vec3 light_direction_view = light_dir_view[i].xyz;
		vec3 light_position_view  = light_pos_view[i].xyz;
		float spot_cos_inner      = light_spot_params[i].x;
		float spot_cos_outer      = light_spot_params[i].y;

		vec3 light_direction;
		float attenuation = 1.0;

		if (light_type == 0) {
			vec3 normalized_light_direction = normalize(light_direction_view);
			light_direction = -normalized_light_direction;
		}
		else {
			vec3 to_light           = light_position_view - position_view;
			float distance_to_light = max(length(to_light), epsilon);
			light_direction         = to_light / distance_to_light;

			float range_safe          = max(range, epsilon);
			float normalized_distance = distance_to_light / range_safe;

			attenuation = 1.0 / (1.0 + normalized_distance * normalized_distance);

			if (light_type == 2) {
				float cone_alignment = dot(normalize(light_direction_view), -light_direction);
				float spot_factor    = smoothstep(spot_cos_outer, spot_cos_inner, cone_alignment);
				attenuation *= spot_factor;
			}
		}

		vec3 half_vector       = normalize(view_direction + light_direction);
		float normal_dot_light = max(dot(shaded_normal_view, light_direction), 0.0);
		float normal_dot_view  = max(dot(shaded_normal_view, view_direction),  0.0);

		float microfacet_distribution = distribution_ggx(shaded_normal_view, half_vector, roughness);
		float microfacet_geometry     = geometry_smith(shaded_normal_view, view_direction, light_direction, roughness);
		vec3 fresnel_term             = fresnel_schlick(max(dot(half_vector, view_direction), 0.0), base_reflectance);

		vec3 specular_numerator    = microfacet_distribution * microfacet_geometry * fresnel_term;
		float specular_denominator = 4.0 * normal_dot_view * normal_dot_light + 0.001;
		vec3 specular              = specular_numerator / specular_denominator;

		vec3 diffuse_weight = (1.0 - fresnel_term) * (1.0 - metallic);

		vec3 irradiance = light_color * intensity * attenuation * normal_dot_light;
		direct_lighting_sum += (diffuse_weight * albedo / pi + specular) * irradiance;
	}

	vec3 ambient_diffuse  = ambient_rgb * albedo * (1.0 - metallic);
	vec3 ambient_specular = ambient_rgb * base_reflectance;
	vec3 color_out        = ambient_diffuse * ambient_occlusion + ambient_specular + direct_lighting_sum + emissive;

	frag_color = vec4(color_out, alpha);
}
@end

@program main vs fs

