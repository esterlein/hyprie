#pragma once

#include "hprint.hpp"

#include "vertex_format.hpp"


namespace hpr::rdr {


inline uint32_t pack_1010102(vec4 value)
{
	uint32_t x = static_cast<uint32_t>((value.x * 0.5f + 0.5f) * 1023.0f);
	uint32_t y = static_cast<uint32_t>((value.y * 0.5f + 0.5f) * 1023.0f);
	uint32_t z = static_cast<uint32_t>((value.z * 0.5f + 0.5f) * 1023.0f);
	uint32_t w = static_cast<uint32_t>((value.w * 0.5f + 0.5f) * 3.0f);

	return (w << 30) | (z << 20) | (y << 10) | x;
}


inline v2u16 pack_uv(vec2 uv)
{
	float x = uv.x - std::floor(uv.x);
	float y = uv.y - std::floor(uv.y);

	return v2u16 {
		static_cast<uint16_t>(x * 65535.0f),
		static_cast<uint16_t>(y * 65535.0f)
	};
}


inline uint32_t pack_color(vec4 color)
{
	const uint32_t r = static_cast<uint8_t>(glm::clamp(color.r, 0.0f, 1.0f) * 255.0f);
	const uint32_t g = static_cast<uint8_t>(glm::clamp(color.g, 0.0f, 1.0f) * 255.0f);
	const uint32_t b = static_cast<uint8_t>(glm::clamp(color.b, 0.0f, 1.0f) * 255.0f);
	const uint32_t a = static_cast<uint8_t>(glm::clamp(color.a, 0.0f, 1.0f) * 255.0f);

	return (a << 24) | (b << 16) | (g << 8) | r;
}

} // hpr::rdr
