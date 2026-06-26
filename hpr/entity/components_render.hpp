#pragma once

#include "hprint.hpp"

#include "math.hpp"


namespace hpr::ecs {


struct ModelComponent
{
	uint32_t prim_first;
	uint32_t prim_count;
};


struct OccluderComponent
{
	uint32_t twin_idx;
	mat4     mtx_L;
};


} // hpr::ecs

