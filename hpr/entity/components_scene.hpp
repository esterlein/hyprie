#pragma once

#include <cstdint>

#include "math.hpp"
#include "entity.hpp"
#include "light_common.hpp"


namespace hpr::ecs {


struct TransformComponent
{
	mat4 world;
	quat rotation;
	vec3 position;
	vec3 scale;
};


struct HierarchyComponent
{
	Entity parent       {invalid_entity};
	Entity first_child  {invalid_entity};
	Entity next_sibling {invalid_entity};
};


struct NameComponent
{
	const char*   text;
	std::uint64_t guid;
};


struct CameraComponent
{
	float   fov_deg;
	float   aspect;
	float   znear;
	float   zfar;
	uint8_t active;
};


struct LightComponent
{
	scn::LightType type;

	uint8_t enabled;
	float   intensity;
	float   range;
	float   inner_deg;
	float   outer_deg;
	vec3    color_rgb;
};

} // hpr::ecs

