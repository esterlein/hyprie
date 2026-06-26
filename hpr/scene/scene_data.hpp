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


struct AABBDOD
{
	float min_x;
	float min_y;
	float min_z;

	float max_x;
	float max_y;
	float max_z;
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


struct ScenePrimitive
{
	rdr::Submesh submesh;

	mat4 mtx_L;
	mat3 mtx_LN;

	uint32_t material_idx;

	ecs::Entity entity;
};

} // hpr::scn

