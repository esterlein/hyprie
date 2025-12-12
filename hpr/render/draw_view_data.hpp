#pragma once

#include "math.hpp"


namespace hpr::rdr {


struct DrawView
{
	mat4  mtx_V;
	mat4  mtx_P;
	mat4  mtx_VP;
	vec3  pos_world;
	vec3  fwd_world;
	float near;
	float far;

	std::array<vec4, math::frustum_plane_count> frustum;
};


struct DrawViewLight
{
	uint8_t type;

	vec3  color_rgb;
	float intensity;
	vec3  dir_view;
	vec3  pos_view;
	float range;
	float cos_inner;
	float cos_outer;

	uint8_t enabled;
};


struct DrawViewLightSet
{
	vec3 ambient_rgb;
	const DrawViewLight* items;
	std::uint32_t count;
};

} // hpr::rdr
