#pragma once

#include <cstdint>

#include "panic.hpp"
#include "mtp_memory.hpp"

#include "tile_data.hpp"
#include "tile_query.hpp"


namespace hpr::scn {


class TileField
{
public:

	using IndexMap = decltype(mtp::make_unordered_map<uint64_t, uint32_t, mtp::default_set>());

public:

	void clear()
	{
		m_chunks.clear();
		m_index.clear();
	}


	[[nodiscard]] bool empty() const
	{
		return m_chunks.empty();
	}


	void resize(int32_t width, int32_t height, int32_t storeys, TileType fill_value = 0)
	{
		clear();

		if (width <= 0 || height <= 0 || storeys <= 0)
			return;

		const int32_t chunk_count_x = (width  + cfg::chunk_size - 1) / cfg::chunk_size;
		const int32_t chunk_count_z = (height + cfg::chunk_size - 1) / cfg::chunk_size;

		// NOTE: mvp - one building
		static constexpr int32_t storey_stack = 0;

		const uint32_t total_chunks =
			static_cast<uint32_t>(chunk_count_x) *
			static_cast<uint32_t>(chunk_count_z) *
			static_cast<uint32_t>(storeys);

		m_chunks.reserve(total_chunks);
		m_index.reserve(total_chunks);

		for (int32_t storey_index = 0; storey_index < storeys; ++storey_index) {
			for (int32_t chunk_z = 0; chunk_z < chunk_count_z; ++chunk_z) {
				for (int32_t chunk_x = 0; chunk_x < chunk_count_x; ++chunk_x) {

					TileChunkCoord chunk_coord {
						.chunk_x      = chunk_x,
						.chunk_z      = chunk_z,
						.storey_index = storey_index,
						.storey_stack = storey_stack
					};

					(void) ensure_chunk(chunk_coord, fill_value);
				}
			}
		}
	}


	[[nodiscard]] TileType get(TileCoord coord) const
	{
		const TileChunkCoord chunk_coord = get_chunk_coord(coord);
		const uint64_t key = get_chunk_coord_hash(chunk_coord);

		const TileChunk* chunk = find_chunk(key);
		if (!chunk)
			return 0;

		const uint32_t idx = local_index(coord, chunk_coord);
		return chunk->tiles[idx];
	}


	void set(TileCoord coord, TileType tile_type)
	{
		const TileChunkCoord chunk_coord = get_chunk_coord(coord);
		TileChunk& chunk = ensure_chunk(chunk_coord, 0);

		const uint32_t idx = local_index(coord, chunk_coord);
		chunk.tiles[idx]   = tile_type;
	}


	[[nodiscard]] TileType* get_ptr(TileCoord coord)
	{
		const TileChunkCoord chunk_coord = get_chunk_coord(coord);
		const uint64_t key = get_chunk_coord_hash(chunk_coord);

		TileChunk* chunk = find_chunk(key);
		if (!chunk)
			return nullptr;

		const uint32_t idx = local_index(coord, chunk_coord);
		return &chunk->tiles[idx];
	}


	[[nodiscard]] const TileType* get_ptr(TileCoord coord) const
	{
		const TileChunkCoord chunk_coord = get_chunk_coord(coord);
		const uint64_t key = get_chunk_coord_hash(chunk_coord);

		const TileChunk* chunk = find_chunk(key);
		if (!chunk)
			return nullptr;

		const uint32_t idx = local_index(coord, chunk_coord);
		return &chunk->tiles[idx];
	}


	[[nodiscard]] TileChunk* find_chunk(uint64_t key)
	{
		auto it = m_index.find(key);
		if (it == m_index.end()) {
			return nullptr;
		}

		const uint32_t index = it->second;
		HPR_ASSERT_MSG(index < m_chunks.size(), "[tilefield] index map out of range");

		return &m_chunks[index];
	}


	[[nodiscard]] const TileChunk* find_chunk(uint64_t key) const
	{
		auto it = m_index.find(key);
		if (it == m_index.end()) {
			return nullptr;
		}

		const uint32_t index = it->second;
		HPR_ASSERT_MSG(index < m_chunks.size(), "[tilefield] index map out of range");

		return &m_chunks[index];
	}

private:

	[[nodiscard]] static uint32_t local_index(TileCoord coord, TileChunkCoord chunk_coord)
	{
		const int32_t base_x = chunk_coord.chunk_x * cfg::chunk_size;
		const int32_t base_z = chunk_coord.chunk_z * cfg::chunk_size;

		const int32_t local_x = coord.x - base_x;
		const int32_t local_z = coord.z - base_z;

		HPR_ASSERT_MSG(local_x >= 0 && local_x < cfg::chunk_size, "[tilefield] local_x out of range");
		HPR_ASSERT_MSG(local_z >= 0 && local_z < cfg::chunk_size, "[tilefield] local_z out of range");

		return static_cast<uint32_t>(local_x) + static_cast<uint32_t>(local_z) * static_cast<uint32_t>(cfg::chunk_size);
	}


	TileChunk& ensure_chunk(TileChunkCoord chunk_coord, TileType fill_value)
	{
		const uint64_t key = get_chunk_coord_hash(chunk_coord);

		if (TileChunk* existing = find_chunk(key)) {
			return *existing;
		}

		TileChunk chunk {};
		chunk.coord = chunk_coord;
		chunk.key   = key;

		const uint32_t count = static_cast<uint32_t>(cfg::chunk_size) * static_cast<uint32_t>(cfg::chunk_size);
		chunk.tiles.reserve(count);

		for (uint32_t i = 0; i < count; ++i) {
			chunk.tiles.emplace_back(fill_value);
		}

		const uint32_t new_index = static_cast<uint32_t>(m_chunks.size());
		m_chunks.emplace_back(std::move(chunk));
		m_index[key] = new_index;

		return m_chunks.back();
	}

private:

	mtp::vault<TileChunk, mtp::default_set> m_chunks;

	IndexMap m_index {mtp::make_unordered_map<uint64_t, uint32_t, mtp::default_set>()};
};


} // hpr::scn

