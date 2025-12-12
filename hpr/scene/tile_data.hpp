#pragma once

#include "math.hpp"

#include "mtp_memory.hpp"


namespace hpr::scn {


using TileType = uint16_t;


namespace cfg {

	inline constexpr int32_t chunk_size = 32;
	inline constexpr float   tile_size  = 0.5f;

} // hpr::scn::cfg


struct TileCoord
{
	int32_t x;
	int32_t z;
	int32_t storey_index;
	int32_t storey_stack;
};


struct TileChunkCoord
{
	int32_t chunk_x;
	int32_t chunk_z;
	int32_t storey_index;
	int32_t storey_stack;
};


struct TileChunk
{
	TileChunkCoord coord {};
	uint64_t       key   {};
	mtp::vault<TileType, mtp::default_set> tiles;
};


struct TileGridParams
{
	vec3  origin_world {0.0f, 0.0f, 0.0f};
	float tile_size    {0.5f};
};


} // hpr::scn
