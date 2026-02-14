#pragma once

#include "math.hpp"
#include "hprint.hpp"

#include "entity.hpp"
#include "render_data.hpp"
#include "light_common.hpp"


namespace hpr::scn {


struct Transform
{
	vec3 position {0.0f};
	quat rotation {1.0f, 0.0f, 0.0f, 0.0f};
	vec3 scale    {1.0f};
};


struct Selection
{
	ecs::Entity entity  {ecs::invalid_entity};
	Transform transform {};
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
	Handle<rdr::Mesh> mesh;
	uint32_t submesh_idx;
	Handle<rdr::MaterialInstance> material;
};

} // hpr::scn

