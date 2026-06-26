#pragma once

#include "hprint.hpp"
#include "math.hpp"


namespace hpr::rdr {


struct SceneBlob
{
	mat4 mtx_M  {1.0f};
	mat4 mtx_MN {1.0f};

	uint32_t material_idx;
	uint32_t pad[3];
};


struct CueBlob
{
	mat4 mtx_M {1.0f};
};


struct OverlayBlob
{
	mat4 mtx_M {1.0f};
	vec4 rgba  {1.0f};
};


struct MaterialBlob
{
	vec4  alb;
	vec4  ems_mtl;
	vec4  rgh_nrm_aos_map;
	vec4  uv_scale_offset;

	v4u32 tex_info_alb;
	v4u32 tex_info_nrm;
	v4u32 tex_info_orm;
	v4u32 tex_info_ems;
};

} // hpr::rdr
