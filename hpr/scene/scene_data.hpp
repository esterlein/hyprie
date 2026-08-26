#pragma once

#include "math.hpp"
#include "hprint.hpp"

#include "entity.hpp"
#include "render_data.hpp"
#include "scene_context.hpp"


namespace hpr::scn {


struct AABB
{
	vec3 min;
	vec3 max;
};


struct Transform
{
	vec3 position {0.0f};
	quat rotation {1.0f, 0.0f, 0.0f, 0.0f};
	vec3 scale    {1.0f};
};


struct Selection
{
	Transform transform {};
	ecs::Entity entity  {ecs::ctx::invalid_entity};
	uint32_t submesh    {std::numeric_limits<uint32_t>::max()};
};


struct SceneLight
{
	scn::LightType type;

	uint8_t enabled;
	float   intensity;
	float   range;
	float   inner_deg;
	float   outer_deg;
	vec3    color_rgb;
};


} // hpr::scn

