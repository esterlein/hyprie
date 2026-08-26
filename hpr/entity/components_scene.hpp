#pragma once

#include <cstdint>

#include "math.hpp"
#include "entity.hpp"
#include "scene_context.hpp"


namespace hpr::ecs {


struct TransformComponent
{
	mat4 mtx_W;

	quat rotation;
	vec3 position;
	vec3 scale;

	vec3 world_fwd() const
	{
		return rotation * vec3(0.0f, 0.0f, -1.0f);
	}
};


struct HierarchyComponent
{
	Entity parent       {ctx::invalid_entity};
	Entity first_child  {ctx::invalid_entity};
	Entity next_sibling {ctx::invalid_entity};
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


struct AnimComponent
{
	uint32_t skeleton_idx;
	uint32_t clip_idx;

	float local_time;

	uint32_t base_pose_idx; 
};

} // hpr::ecs

