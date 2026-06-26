#pragma once

#include "math.hpp"


namespace hpr::geo {


struct Quad
{
	float half_extent {0.5f};
};


struct Box
{
	vec3 min {};
	vec3 max {};
};


struct Ring
{
	uint32_t segment_count {16};
	float radius           {1.0f};
	float thickness        {0.1f};
};


struct RingSolid
{
	uint32_t segment_count {16};
	float radius           {1.0f};
	float radial_thickness {0.1f};
	float height           {1.0f};
};


struct Cone
{
	uint32_t segment_count {16};
	float base_radius      {1.0f};
	float base_z           {0.0f};
	float apex_z           {1.0f};
};


struct Arrow
{
	uint32_t cone_segments {16};
	float shaft_length     {1.0f};
	float shaft_radius     {0.1f};
	float tip_length       {0.3f};
	float tip_radius       {0.2f};
};


struct Diamond
{
	float width         {0.5f};
	float height        {1.0f};
	float depth         {0.5f};
	vec3  center_offset {0.0f, 0.0f, 0.0f}; 
};


} // hpr::geo
