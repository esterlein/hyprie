#pragma once

#include "hprint.hpp"

#include "math.hpp"


namespace hpr::rdr {


inline vec2 pos_world_to_px(const vec3& pos_W, const mat4& mtx_VP, float width, float height)
{
	const vec4 clip = mtx_VP * vec4(pos_W, 1.0f);

	const vec3 ndc = vec3(clip) / clip.w;
	return vec2 {
		(ndc.x * 0.5f + 0.5f) * width,
		(1.0f - (ndc.y * 0.5f + 0.5f)) * height
	};
}


inline float world_size_per_px(
	const vec3& pos_W,
	const mat4& mtx_V,
	const mat4& mtx_P,
	uint32_t    screen_height
)
{
	const vec4  pos_V = mtx_V * vec4(pos_W, 1.0f);
	const float pos_z = glm::max(0.0f, -pos_V.z);

	const float height_px = static_cast<float>(screen_height > 0 ? screen_height : 1);

	const bool is_perspective =
		glm::epsilonEqual(mtx_P[2][3], -1.0f, math::projection_epsilon) ||
		glm::epsilonEqual(mtx_P[3][3],  0.0f, math::projection_epsilon);

	const float y_scale_P = mtx_P[1][1];

	if (is_perspective) {
		const float tan_half_y_fov = 1.0f / y_scale_P;
		return (2.0f * pos_z * tan_half_y_fov) / height_px;
	}
	else {
		const float height_W = 2.0f / y_scale_P;
		return height_W / height_px;
	}
}


inline uint32_t rgb_to_u32(const vec3& rgb)
{
	auto to_byte = [](float color_value)
	{
		if (color_value <= 0.f)
			return 0U;
		if (color_value >= 1.f)
			return 255U;

		return static_cast<uint32_t>(color_value * 255.f + 0.5f);
	};

	uint32_t r = to_byte(rgb.r);
	uint32_t g = to_byte(rgb.g);
	uint32_t b = to_byte(rgb.b);

	return (r << 16) | (g << 8) | b;
}


inline vec3 rgb_from_u32(uint32_t rgb)
{
	constexpr float inv_255 = 1.0f / 255.0f;

	float r = static_cast<float>((rgb >> 16) & 0xFF) * inv_255;
	float g = static_cast<float>((rgb >> 8)  & 0xFF) * inv_255;
	float b = static_cast<float>( rgb        & 0xFF) * inv_255;

	return vec3(r, g, b);
}


inline void frgb_from_u32(uint32_t u32_rgb, float* frgb)
{
	vec3 rgb = rgb_from_u32(u32_rgb);

	frgb[0] = rgb.r;
	frgb[1] = rgb.g;
	frgb[2] = rgb.b;
}

} // hpr::rdr

