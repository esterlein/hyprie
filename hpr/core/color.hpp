#pragma once

#include <cstdint>

#include "math.hpp"


namespace hpr {


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

} // hpr

