#pragma once

#include <cstdint>

#include "math.hpp"


namespace hpr::ecs {


struct ModelComponent
{
	uint32_t submesh_first;
	uint32_t submesh_count;
};


struct BoundComponent
{
	vec3 local_center;
	vec3 local_half;
	vec3 world_center;
	vec3 world_half;
};

} // hpr::ecs

