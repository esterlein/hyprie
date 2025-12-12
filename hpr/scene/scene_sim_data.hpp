#pragma once

#include "mtp_memory.hpp"

#include "stratum.hpp"
#include "tile_field.hpp"
#include "voxel_field.hpp"
#include "ghost_infra.hpp"
#include "storey_data.hpp"
#include "tile_draw_data.hpp"


namespace hpr::scn {


struct SceneSimData
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


} // hpr::scn

