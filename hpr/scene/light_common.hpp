#pragma once

#include <cstdint>


namespace hpr::scn {


inline constexpr uint32_t k_max_light_count = 16;


enum class LightType : uint8_t
{
	Directional = 0,
	Point = 1,
	Spot = 2,
};

} // hpr::scn
