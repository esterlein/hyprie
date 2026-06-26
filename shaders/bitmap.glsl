@vs vs

layout(location = 0) in vec2 position_in;
layout(location = 1) in vec2 uv_in;
layout(location = 2) in vec4 color_in;

layout(set = 0, binding = 0) uniform vs_params
{
	mat4 mtx_P_ortho;
};

layout(location = 0) out vec2 uv;
layout(location = 1) out vec4 color;


void main()
{
	gl_Position = mtx_P_ortho * vec4(position_in, 0.0, 1.0);
	uv          = uv_in;
	color       = color_in;
}
@end


@fs fs

layout(location = 0) in vec2 uv;
layout(location = 1) in vec4 color;

layout(set = 0, binding = 0) uniform texture2D u_font_tex;
layout(set = 0, binding = 1) uniform sampler   u_font_smp;

layout(location = 0) out vec4 frag_color;


void main()
{
	vec4 texel = texture(sampler2D(u_font_tex, u_font_smp), uv);
	float coverage  = min(texel.a, texel.r);
	frag_color = vec4(color.rgb, color.a * coverage);
}
@end

@program main vs fs

