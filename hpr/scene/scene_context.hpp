#pragma once

#include "handle.hpp"
#include "texture_data.hpp"
#include "draw_view_data.hpp"


namespace hpr::scn {


namespace cfg {

inline constexpr uint32_t max_light_count = 16U;

} // hpr::scn::cfg


enum class LightType : uint8_t
{
	Directional = 0,
	Point       = 1,
	Spot        = 2,
};


struct SceneContext
{
	rdr::DrawView draw_view {};
	rdr::LightSet light_set {};

	float delta_time {0.0f};
};


} // hpr

