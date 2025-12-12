#pragma once

#include <cstdint>

#include "mtp_memory.hpp"

#include "math.hpp"
#include "tile_data.hpp"
#include "render_data.hpp"


namespace hpr::rdr {


struct TileStyle
{
	Handle<Texture> palette;

	vec4 tile_params;
	vec4 border_color;
	int  chunk_size {32};
};


struct TileChunkDrawable
{
	Handle<Mesh>      mesh;
	uint32_t          submesh_idx;
	Handle<Texture>   tilemap;
	Handle<TileStyle> tile_style;

	scn::TileChunkCoord coord;

	mat4 mtx_M;

	vec3 bounds_center;
	vec3 bounds_half;

	uint64_t coord_hash;

	bool dirty {true};
};


struct TileChunkDrawableSet
{
	mtp::vault<TileChunkDrawable, mtp::default_set> drawables;

	bool enabled {true};

	int32_t storey_min {0};
	int32_t storey_max {0};

	Handle<TileStyle> tile_style;
};


} // hpr::rdr

