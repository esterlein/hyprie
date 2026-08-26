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

layout(set = 0, binding = 0) uniform texture2D u_equirect;
layout(set = 0, binding = 0) uniform sampler u_sampler;

layout(location = 0) out vec4 frag_color;

const vec2 inv_atan = vec2(0.1591, 0.3183);

vec2 sample_spherical_map(vec3 direction)
{
	vec2 uv_coord = vec2(atan(direction.z, direction.x), asin(direction.y));
	uv_coord *= inv_atan;
	uv_coord += 0.5;
	return uv_coord;
}

void main()
{
	vec3 normal_dir = normalize(local_pos_fs);
	vec2 uv_coord = sample_spherical_map(normal_dir);
	vec3 color_rgb = texture(sampler2D(u_equirect, u_sampler), uv_coord).rgb;
	
	frag_color = vec4(color_rgb, 1.0);
}
@end

@program main vs fs
