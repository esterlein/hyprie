#pragma once

#include "hprint.hpp"

#include "mtp_memory.hpp"
#include "math.hpp"

#include "render_data.hpp"
#include "scene_data.hpp"
#include "anim_data.hpp"
#include "tile_data.hpp"
#include "bvh_data.hpp"

#include "stratum.hpp"
#include "tile_field.hpp"
#include "voxel_field.hpp"
#include "ghost_infra.hpp"
#include "storey_data.hpp"
#include "geometry_data.hpp"
#include "draw_tile_data.hpp"
#include <cstdint>


namespace hpr::scn {


struct SceneRenderRig
{
	mtp::vault<rdr::Submesh, mtp::default_set> stat_submeshes;
	mtp::vault<mat4,         mtp::default_set> stat_mtxs_L;
	mtp::vault<mat3,         mtp::default_set> stat_mtxs_LN;
	mtp::vault<uint32_t,     mtp::default_set> stat_material_idxs;
	mtp::vault<ecs::Entity,  mtp::default_set> stat_entities;

	mtp::vault<AABB,         mtp::default_set> stat_aabb_L;
	mtp::vault<mat4,         mtp::default_set> stat_mtxs_M;
	mtp::vault<uint32_t,     mtp::default_set> stat_occludee_idxs;

	mtp::vault<uint32_t,     mtp::default_set> stat_ecs_blob_idxs;
	mtp::vault<uint32_t,     mtp::default_set> stat_sbms_visible;

	mtp::vault<float,        mtp::default_set> stat_aabb_x_min_W;
	mtp::vault<float,        mtp::default_set> stat_aabb_y_min_W;
	mtp::vault<float,        mtp::default_set> stat_aabb_z_min_W;
	mtp::vault<float,        mtp::default_set> stat_aabb_x_max_W;
	mtp::vault<float,        mtp::default_set> stat_aabb_y_max_W;
	mtp::vault<float,        mtp::default_set> stat_aabb_z_max_W;

	mtp::vault<rdr::Submesh, mtp::default_set> skin_submeshes;
	mtp::vault<mat4,         mtp::default_set> skin_mtxs_L;
	mtp::vault<mat3,         mtp::default_set> skin_mtxs_LN;
	mtp::vault<uint32_t,     mtp::default_set> skin_material_idxs;
	mtp::vault<ecs::Entity,  mtp::default_set> skin_entities;

	mtp::vault<AABB,         mtp::default_set> skin_aabb_L;
	mtp::vault<mat4,         mtp::default_set> skin_mtxs_M;
	mtp::vault<uint32_t,     mtp::default_set> skin_occludee_idxs;

	mtp::vault<uint32_t,     mtp::default_set> skin_ecs_blob_idxs;

	vec3 ambient_rgb {0.0f, 0.0f, 0.0f};

	void clear_volatile()
	{
		stat_aabb_x_min_W.clear();
		stat_aabb_y_min_W.clear();
		stat_aabb_z_min_W.clear();
		stat_aabb_x_max_W.clear();
		stat_aabb_y_max_W.clear();
		stat_aabb_z_max_W.clear();

		stat_ecs_blob_idxs.clear();
		stat_sbms_visible.clear();
	}

	void resize_aabb_world(size_t size)
	{
		stat_aabb_x_min_W.resize(size);
		stat_aabb_y_min_W.resize(size);
		stat_aabb_z_min_W.resize(size);
		stat_aabb_x_max_W.resize(size);
		stat_aabb_y_max_W.resize(size);
		stat_aabb_z_max_W.resize(size);
	}
};


struct SceneCullRig
{
	mtp::vault<vec3,          mtp::default_set> hull_positions;
	mtp::vault<uint32_t,      mtp::default_set> hull_indices;
	mtp::vault<geo::Geoslice, mtp::default_set> hull_geoslices;

	mtp::vault<vec3,          mtp::default_set> twin_positions;
	mtp::vault<uint32_t,      mtp::default_set> twin_indices;
	mtp::vault<geo::Geoslice, mtp::default_set> twin_geoslices;

	mtp::vault<uint32_t,      mtp::default_set> occluder_idxs;
	mtp::vault<mat4,          mtp::default_set> mtxs_MVP_occluder;

	mtp::vault<rdr::Submesh,  mtp::default_set> hull_subwires;
	mtp::vault<rdr::Submesh,  mtp::default_set> twin_subwires;

	void clear_volatile()
	{
		occluder_idxs.clear();
		mtxs_MVP_occluder.clear();
	}
};


struct SceneAnimRig
{
	mtp::vault<anm::Skeleton,  mtp::default_set> skeletons;
	mtp::vault<anm::AnimClip,  mtp::default_set> clips;
	mtp::vault<anm::AnimTrack, mtp::default_set> tracks;
	mtp::vault<float,          mtp::default_set> key_times;
	mtp::vault<vec3,           mtp::default_set> key_tsls;
	mtp::vault<quat,           mtp::default_set> key_rots;

	mtp::vault<int16_t,        mtp::default_set> parent_idxs;
	mtp::vault<mat4,           mtp::default_set> mtxs_inv_bind;
	mtp::vault<mat4,           mtp::default_set> mtxs_L_rest;

	mtp::vault<uint16_t, mtp::default_set> track_caches;

	mtp::vault<mat4,     mtp::default_set> mtxs_M_bones;

	void clear_volatile()
	{
		mtxs_M_bones.clear();
	}
};


struct SceneSpatialRig
{
	mtp::vault<vec3,            mtp::default_set> positions;
	mtp::vault<uint32_t,        mtp::default_set> indices;

	mtp::vault<geo::BLBVH8Node, mtp::default_set> blas_nodes;
	mtp::vault<geo::TLBVH8Node, mtp::default_set> tlas_nodes;

	mtp::vault<uint32_t,        mtp::default_set> blas_sbm_roots;
	mtp::vault<uint32_t,        mtp::default_set> tlas_sbm_leaves;

	uint32_t tlas_root_idx {0xFFFFFFFFU};

	void clear_volatile()
	{

	}
};


struct SceneSimRig
{
	Stratum        stratum;
	TileField      tilefield;
	TileGridParams grid_params;

	rdr::TileChunkDrawableSet draw_data;

	mtp::vault<StoreyStackSpec, mtp::default_set> storey_stack_specs;

	VoxelGridParams voxel_grid;
	VoxelField      voxelfield;

	GhostInfra ghost_infra;
};


struct Scene
{
	SceneRenderRig  render_rig;
	SceneCullRig    cull_rig;
	SceneAnimRig    anim_rig;
	SceneSpatialRig spatial_rig;
	SceneSimRig     sim_rig;

	void clear_volatile()
	{
		render_rig.clear_volatile();
		cull_rig.clear_volatile();
		anim_rig.clear_volatile();
		spatial_rig.clear_volatile();
	}
};


} // hpr::scn
