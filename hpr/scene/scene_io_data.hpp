#pragma once

#include <array>
#include <string>
#include <cstdint>

#include "math.hpp"
#include "mtp_memory.hpp"
#include "light_common.hpp"


namespace hpr::scn::io {


struct TransformDoc
{
	vec3 position;
	quat rotation;
	vec3 scale;
};


struct ModelDoc
{
	std::string mesh_path;
};


struct CameraDoc
{
	float fov_deg;
	float znear;
	float zfar;
	bool  active;
};


struct LightDoc
{
	LightType type;

	vec3  color_rgb;
	float intensity;
	float range;
	float inner_deg;
	float outer_deg;
	bool  enabled;
};


struct ComponentDoc
{
	enum class ComponentKind : std::uint8_t
	{
		Transform = 0,
		Model     = 1,
		Camera    = 2,
		Light     = 3
	};

	ComponentKind kind;
	std::variant<TransformDoc, ModelDoc, CameraDoc, LightDoc> payload;
};


struct EntityDoc
{
	std::string guid;
	std::string name;
	std::string parent_guid;

	mtp::vault<ComponentDoc, mtp::default_set> components;
};


struct SceneDoc
{
	vec3 ambient_rgb;

	mtp::vault<EntityDoc, mtp::default_set> entity_docs;
};

} // hpr::scn::io

