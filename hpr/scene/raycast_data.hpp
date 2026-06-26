#pragma once

#include "hprint.hpp"


#include "math.hpp"

#include "scene.hpp"

#include "render_context.hpp"

#include <limits>



namespace hpr::scn {


struct Ray
{
	vec3 origin;
	vec3 direction;
};


struct RayHit
{
	bool        is_hit               {false};
	ecs::Entity entity               {ecs::ctx::invalid_entity};
	uint32_t    submesh              {std::numeric_limits<uint32_t>::max()};
	float       closest_hit_dist {std::numeric_limits<float>::infinity()};
};


struct RaycastAsyncResult
{
	RayHit ray_hit {};

	static_assert(sizeof(RayHit) <= 64U);
	uint8_t pad[64U - sizeof(RayHit)]; 
};


struct RaycastJobSlice
{
	uint32_t begin;
	uint32_t end;

	Ray  ray;
	vec3 inv_direction;

	const scn::SceneRenderRig* rig;
	const rdr::StagingContext* staging_ctx;

	RaycastAsyncResult* async_result;
};


} // hpr::scn

