#pragma once

#include "hprint.hpp"
#include "math.hpp"
#include "entity.hpp"

#include <limits>


namespace hpr::scn {


namespace cfg {

inline constexpr uint32_t bvh_max_depth = 64U;

} // hpr::scn::cfg


struct Ray
{
	vec3 origin;
	vec3 direction;
};

struct PickRayCtx
{
	Ray  ray        {};
	bool is_pending {false};

	uint32_t tested_node_idxs[cfg::bvh_max_depth];
	uint32_t path_node_idxs[cfg::bvh_max_depth];

	uint32_t tested_node_cnt {0};
	uint32_t path_node_cnt   {0};
};

struct RayHit
{
	bool        is_hit           {false};
	ecs::Entity entity           {ecs::ctx::invalid_entity};
	uint32_t    submesh          {std::numeric_limits<uint32_t>::max()};
	float       closest_hit_dist {std::numeric_limits<float>::infinity()};

	uint32_t tested_node_idxs[cfg::bvh_max_depth];
	uint32_t tested_node_cnt {0};

	uint32_t path_node_idxs[cfg::bvh_max_depth];
	uint32_t path_node_cnt {0};
};


} // hpr::scn

