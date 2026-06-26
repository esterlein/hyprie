#pragma once

#include "geometry_data.hpp"
#include "hprint.hpp"

#include <array>

#include "sokol_gfx.h"
#include "mtp_memory.hpp"

#include "math.hpp"
#include "handle.hpp"

#include "texture_data.hpp"


namespace hpr::rdr {


namespace cfg {

inline constexpr uint8_t max_tex_per_mat = 4;

} // hpr::rdr::cfg


struct Submesh
{
	uint32_t vtx_base;
	uint32_t idx_first;
	uint32_t idx_count;
};


struct Mesh
{
	uint32_t vtx_base;
	uint32_t vtx_count;
	uint32_t idx_first;
	uint32_t idx_count;

	mtp::vault<Submesh, mtp::default_set> submeshes;
};


struct MaterialMap
{
	enum class Slot : uint8_t
	{
		alb = 0,
		nrm,
		orm,
		ems,
		cnt
	};

	enum class Flag : uint8_t
	{
		alb = 1U << static_cast<uint8_t>(Slot::alb),
		nrm = 1U << static_cast<uint8_t>(Slot::nrm),
		orm = 1U << static_cast<uint8_t>(Slot::orm),
		ems = 1U << static_cast<uint8_t>(Slot::ems)
	};
};

using MatMapSlot = MaterialMap::Slot;
using MatMapFlag = MaterialMap::Flag;


struct ORMH
{
	enum class Slot : uint8_t
	{
		occ = 0,
		rgh,
		mtl,
		hgt
	};
};


enum class AlphaMode : uint8_t
{
	opaque = 0,
	mask   = 1,
	blend  = 2
};


struct MaterialTemplate
{
	std::array<Handle<Texture>, cfg::max_tex_per_mat> textures;
	std::array<uint8_t,         cfg::max_tex_per_mat> uv_index;

	int32_t map_mask;

	AlphaMode alpha_mode {AlphaMode::opaque};
};


struct MaterialInstance
{
	Handle<MaterialTemplate> mat_template;

	uint32_t ssbo_idx;
	int32_t  map_mask;

	vec4 albedo_tint;

	float metallic_factor;
	float roughness_factor;
	float ao_strength;
	float normal_scale;

	vec3 emissive_factor;

	vec2 uv_scale;
	vec2 uv_offset;
};


struct Model
{
	mtp::vault<Handle<Mesh>,             mtp::default_set> meshes;
	mtp::vault<Handle<MaterialInstance>, mtp::default_set> materials;

	mtp::vault<geo::Geoslice, mtp::default_set> hull_geoslices;
	mtp::vault<rdr::Submesh,  mtp::default_set> hull_subwires;
	mtp::vault<vec3,          mtp::default_set> hull_positions;
	mtp::vault<uint32_t,      mtp::default_set> hull_indices;

	geo::Geoslice                          twin_geoslice;
	rdr::Submesh                           twin_subwire;
	mtp::vault<vec3,     mtp::default_set> twin_positions;
	mtp::vault<uint32_t, mtp::default_set> twin_indices;

	uint32_t occludee_hull_base_idx {0xFFFFFFFF};
	uint32_t occluder_twin_idx      {0xFFFFFFFF};
};


} // hpr::rdr

