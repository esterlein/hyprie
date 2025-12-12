#pragma once

#include "math.hpp"

#include "stratum.hpp"
#include "tile_data.hpp"
#include "tile_field.hpp"
#include "storey_data.hpp"
#include "tile_draw_data.hpp"


namespace hpr::scn {


[[nodiscard]] inline TileCoord world_to_tile(const vec3& pos_world, const TileGridParams& grid)
{
	const vec3 local = pos_world - grid.origin_world;

	const float size_inv = 1.0f / grid.tile_size;

	const int32_t x = static_cast<int32_t>(floorf(local.x * size_inv));
	const int32_t z = static_cast<int32_t>(floorf(local.z * size_inv));

	const int32_t storey_index = static_cast<int32_t>(floorf(local.y * size_inv));

	// NOTE: mvp - one building
	static constexpr int32_t storey_stack = 0;

	return TileCoord {x, z, storey_index, storey_stack};
}


[[nodiscard]] inline vec3 tile_to_world_center(const TileCoord& coord, const TileGridParams& grid)
{
	const float x = grid.origin_world.x + (static_cast<float>(coord.x)            + 0.5f) * grid.tile_size;
	const float y = grid.origin_world.y + (static_cast<float>(coord.storey_index) + 0.5f) * grid.tile_size;
	const float z = grid.origin_world.z + (static_cast<float>(coord.z)            + 0.5f) * grid.tile_size;

	return vec3 {x, y, z};
}


[[nodiscard]] inline TileChunkCoord get_chunk_coord(TileCoord coord)
{
	const int32_t chunk_x = math::floor_div(coord.x, cfg::chunk_size);
	const int32_t chunk_z = math::floor_div(coord.z, cfg::chunk_size);

	return TileChunkCoord {
		.chunk_x      = chunk_x,
		.chunk_z      = chunk_z,
		.storey_index = coord.storey_index,
		.storey_stack = coord.storey_stack
	};
}


[[nodiscard]] inline uint64_t get_chunk_coord_hash(TileChunkCoord coord)
{
	static constexpr uint64_t fnv1a_offset_basis = 14695981039346656037ULL;
	static constexpr uint64_t fnv1a_prime        = 1099511628211ULL;

	uint64_t fnv1a_hash = fnv1a_offset_basis;

	const uint32_t stack   = static_cast<uint32_t>(coord.storey_stack);
	const uint32_t storey  = static_cast<uint32_t>(coord.storey_index);
	const uint32_t chunk_x = static_cast<uint32_t>(coord.chunk_x);
	const uint32_t chunk_z = static_cast<uint32_t>(coord.chunk_z);

	fnv1a_hash ^= stack;   fnv1a_hash *= fnv1a_prime;
	fnv1a_hash ^= storey;  fnv1a_hash *= fnv1a_prime;
	fnv1a_hash ^= chunk_x; fnv1a_hash *= fnv1a_prime;
	fnv1a_hash ^= chunk_z; fnv1a_hash *= fnv1a_prime;

	return fnv1a_hash;
}


inline void mark_dirty_chunk(
	const Stratum&             stratum,
	const TileGridParams&      grid_params,
	const TileCoord            tile_coord,
	rdr::TileChunkDrawableSet& chunk_drawable_set
)
{
	const TileChunkCoord chunk_coord = get_chunk_coord(tile_coord);
	const uint64_t coord_hash = get_chunk_coord_hash(chunk_coord);

	const Storey* storey = stratum.find_storey(chunk_coord.storey_stack, chunk_coord.storey_index);
	HPR_ASSERT_MSG(storey, "missing storey for chunk");

	const int32_t base_x = chunk_coord.chunk_x * cfg::chunk_size;
	const int32_t base_z = chunk_coord.chunk_z * cfg::chunk_size;

	const float min_x = static_cast<float>(base_x) * cfg::tile_size;
	const float min_z = static_cast<float>(base_z) * cfg::tile_size;

	const float size_x = static_cast<float>(cfg::chunk_size) * cfg::tile_size;
	const float size_z = static_cast<float>(cfg::chunk_size) * cfg::tile_size;

	const float min_y =
		grid_params.origin_world.y + static_cast<float>(storey->voxel_y_beg) * grid_params.tile_size;

	const vec3 bounds_half {
		size_x * 0.5f,
		math::aabb_plane_half_thickness,
		size_z * 0.5f
	};

	const vec3 bounds_center {
		min_x + bounds_half.x,
		min_y,
		min_z + bounds_half.z
	};

	const mat4 mtx_M =
		glm::translate(mat4(1.0f), vec3(min_x, min_y, min_z)) *
		glm::scale(mat4(1.0f), vec3(size_x, 1.0f, size_z));

	// TODO: replace o(n) per chunk edit with an index map

	for (auto& chunk_drawable : chunk_drawable_set.drawables) {
		if (chunk_drawable.coord_hash == coord_hash) {

			chunk_drawable.coord         = chunk_coord;
			chunk_drawable.tile_style    = chunk_drawable_set.tile_style;
			chunk_drawable.mtx_M         = mtx_M;
			chunk_drawable.bounds_center = bounds_center;
			chunk_drawable.bounds_half   = bounds_half;
			chunk_drawable.dirty         = true;

			return;
		}
	}

	const rdr::TileChunkDrawable chunk_drawable {
		.mesh          = Handle<rdr::Mesh>::null(),
		.submesh_idx   = 0,
		.tile_style    = chunk_drawable_set.tile_style,
		.coord         = chunk_coord,
		.mtx_M         = mtx_M,
		.bounds_center = bounds_center,
		.bounds_half   = bounds_half,
		.coord_hash    = coord_hash,
		.dirty         = true
	};

	chunk_drawable_set.drawables.emplace_back(chunk_drawable);
}


} // hpr::scn

