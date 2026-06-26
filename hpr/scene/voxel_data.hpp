#pragma once

#include <cstdint>

#include "math.hpp"

#include "mtp_memory.hpp"


namespace hpr::scn {


using VoxelType = uint16_t;


struct VoxelCoord
{
	int32_t x;
	int32_t y;
	int32_t z;
};


struct VoxelChunkCoord
{
	int32_t chunk_x;
	int32_t chunk_y;
	int32_t chunk_z;
};


struct VoxelGridParams
{
	vec3  origin_world {0.0f, 0.0f, 0.0f};
	float voxel_size   {0.5f};
};


struct VoxelChunk
{
	VoxelChunkCoord coord;
	uint64_t        key;

	// NOTE: ? storage: dense/sparse cube / rle / palette

	mtp::vault<VoxelType, mtp::default_set> voxels;
};


struct VoxelWorld
{
	static constexpr int32_t k_chunk_size = 32;

	using IndexMap = decltype(mtp::make_unordered_map<uint64_t, uint32_t, mtp::default_set>());

	mtp::vault<VoxelChunk, mtp::default_set> chunks;

	IndexMap index {mtp::make_unordered_map<uint64_t, uint32_t, mtp::default_set>()};
};


} // hpr::scn

