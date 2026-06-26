#pragma once

#include "math.hpp"


namespace hpr::rdr {


struct DrawView
{
	mat4  mtx_V;
	mat4  mtx_P;
	mat4  mtx_VP;
	vec3  pos_W;
	vec3  fwd_W;
	float near;
	float far;

	std::array<vec4, math::frustum_plane_cnt> frustum;
};


struct LightItem
{
	uint8_t type;

	vec3  color_rgb;
	float intensity;
	vec3  dir_view;
	vec3  pos_view;
	vec3  dir_world;
	vec3  pos_world;
	float range;
	float cos_inner;
	float cos_outer;

	uint8_t enabled;
};


struct LightSet
{
	vec3 ambient_rgb;
	const LightItem* items;
	std::uint32_t    count;
};

} // hpr::rdr
