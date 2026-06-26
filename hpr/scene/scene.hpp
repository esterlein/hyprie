#pragma once

#include "hprint.hpp"

#include "mtp_memory.hpp"
#include "math.hpp"

#include "render_data.hpp"
#include "scene_data.hpp"
#include "tile_data.hpp"

#include "stratum.hpp"
#include "tile_field.hpp"
#include "voxel_field.hpp"
#include "ghost_infra.hpp"
#include "storey_data.hpp"
#include "geometry_data.hpp"
#include "draw_tile_data.hpp"


namespace hpr::scn {


struct SceneRenderRig
{
	mtp::vault<ScenePrimitive, mtp::default_set> primitives;
	mtp::vault<AABB,           mtp::default_set> aabb_local;
	mtp::vault<mat4,           mtp::default_set> matrices_M;

	mtp::vault<uint32_t, mtp::default_set> occludee_idxs;

	mtp::vault<float, mtp::default_set> aabb_world_min_x;
	mtp::vault<float, mtp::default_set> aabb_world_min_y;
	mtp::vault<float, mtp::default_set> aabb_world_min_z;
	mtp::vault<float, mtp::default_set> aabb_world_max_x;
	mtp::vault<float, mtp::default_set> aabb_world_max_y;
	mtp::vault<float, mtp::default_set> aabb_world_max_z;

	mtp::vault<uint32_t, mtp::default_set> ecs_trs_idxs;
	mtp::vault<uint32_t, mtp::default_set> prims_visible;

	vec3 ambient_rgb {0.0f, 0.0f, 0.0f};

	void clear_volatile()
	{
		aabb_world_min_x.clear();
		aabb_world_min_y.clear();
		aabb_world_min_z.clear();
		aabb_world_max_x.clear();
		aabb_world_max_y.clear();
		aabb_world_max_z.clear();

		ecs_trs_idxs.clear();
		prims_visible.clear();
	}

	void resize_aabb_world(size_t size)
	{
		aabb_world_min_x.resize(size);
		aabb_world_min_y.resize(size);
		aabb_world_min_z.resize(size);
		aabb_world_max_x.resize(size);
		aabb_world_max_y.resize(size);
		aabb_world_max_z.resize(size);
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
	mtp::vault<mat4,          mtp::default_set> occluder_matrices_MVP;

	mtp::vault<rdr::Submesh,  mtp::default_set> hull_subwires;
	mtp::vault<rdr::Submesh,  mtp::default_set> twin_subwires;

	void clear_volatile()
	{
		occluder_idxs.clear();
		occluder_matrices_MVP.clear();
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
	SceneRenderRig render_rig;
	SceneCullRig   cull_rig;
	SceneSimRig    sim_rig;

	void clear_volatile()
	{
		render_rig.clear_volatile();
		cull_rig.clear_volatile();
	}
};


} // hpr::scn
