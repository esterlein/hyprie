#pragma once

#include "hprint.hpp"
#include "mtp_memory.hpp"

#include "math.hpp"
#include "handle.hpp"
#include "geometry_data.hpp"
#include "resource_ingest.hpp"

#include "cgltf.h"


namespace hpr::res {


struct GltfResource
{
	cgltf_data* data {nullptr};
};


struct VtxAttribute
{
	mtp::vault<uint8_t, mtp::default_set> blob;

	AttrFormat format;
	uint8_t    component_count;
};


enum : uint32_t
{
	tex_albedo = 0,
	tex_normal,
	tex_ormh,
	tex_emissive,
	max_tex_per_mat,
	tex_environment
};


struct ImageResource
{
	uint32_t width    {0};
	uint32_t height   {0};
	uint32_t channels {0};
	uint32_t type     {tex_albedo};

	mtp::vault<uint8_t, mtp::default_set> pixels;
};


struct MaterialResource
{
	vec4  albedo_tint      {1.0f, 1.0f, 1.0f, 1.0f};
	float metallic_factor  {1.0f};
	float roughness_factor {1.0f};
	float ao_strength      {1.0f};
	float normal_scale     {1.0f};
	vec3  emissive_factor  {0.0f, 0.0f, 0.0f};

	uint32_t map_mask {0};
	int8_t   uv_index[max_tex_per_mat] {0, 0, 0, 0};

	Handle<ImageResource> textures[max_tex_per_mat];

	bool has_albedo()   const { return (map_mask & (1U << tex_albedo))   != 0U; }
	bool has_normal()   const { return (map_mask & (1U << tex_normal))   != 0U; }
	bool has_ormh()     const { return (map_mask & (1U << tex_ormh))     != 0U; }
	bool has_emissive() const { return (map_mask & (1U << tex_emissive)) != 0U; }
};


struct ImportNode
{
	mat4 mtx_trs;

	uint32_t parent;
	uint32_t mesh;

	uint32_t child_first;
	uint32_t child_count;
};


struct ImportHull
{
	uint32_t vtx_base;
	uint32_t vtx_count;
	uint32_t idx_base;
	uint32_t idx_count;
};


struct ImportSubmesh
{
	uint32_t vtx_base;
	uint32_t idx_first;
	uint32_t idx_count;

	uint32_t model_mat_idx;

	uint32_t hull_idx;
};


struct ImportMesh
{
	std::array<VtxAttribute, static_cast<size_t>(AttrType::count)> vtx_attributes;

	mtp::vault<uint32_t,      mtp::default_set> indices;
	mtp::vault<ImportSubmesh, mtp::default_set> submeshes;
};


struct ImportAnimTrack
{
	uint32_t node_idx;
	mtp::vault<float, mtp::default_set> times;
	mtp::vault<vec3,  mtp::default_set> translations;
	mtp::vault<quat,  mtp::default_set> rotations;
};


struct ImportAnimClip
{
	float duration_ticks {0.0f};
	mtp::vault<ImportAnimTrack, mtp::default_set> tracks;
};


struct ImportSkeleton
{
	mtp::vault<uint32_t, mtp::default_set> joint_node_idxs;
	mtp::vault<mat4,     mtp::default_set> mtxs_inv_bind;
	mtp::vault<mat4,     mtp::default_set> mtxs_L_rest;
};


struct ImportModel
{
	mtp::vault<ImportMesh,                    mtp::default_set> meshes;
	mtp::vault<ImportNode,                    mtp::default_set> nodes;
	mtp::vault<Handle<res::MaterialResource>, mtp::default_set> materials;

	mtp::vault<uint32_t, mtp::default_set> node_child_idxs;

	mtp::vault<ImportHull, mtp::default_set> hulls;
	mtp::vault<vec3,       mtp::default_set> hull_positions;
	mtp::vault<uint32_t,   mtp::default_set> hull_indices;

	bool is_occluder    {false};
	mat4 mtx_L_occluder {1.0f};

	geo::Geoslice occluder_twin {};

	mtp::vault<vec3,     mtp::default_set> twin_positions;
	mtp::vault<uint32_t, mtp::default_set> twin_indices;

	mtp::vault<ImportSkeleton, mtp::default_set> skins;
	mtp::vault<ImportAnimClip, mtp::default_set> animations;
};


} // hpr::res

