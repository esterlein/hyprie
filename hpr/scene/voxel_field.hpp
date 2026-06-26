#pragma once

#include <cstdint>

#include "panic.hpp"
#include "mtp_memory.hpp"

#include "math.hpp"

#include "voxel_data.hpp"


namespace hpr::scn {


class VoxelField
{
public:

	static constexpr int32_t k_chunk_size = 32;

	struct VoxelChunk
	{
		VoxelChunkCoord coord {};
		uint64_t        key   {};
		mtp::vault<VoxelType, mtp::default_set> voxels;
	};

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

	[[nodiscard]] static int32_t floor_div(int32_t value, int32_t divisor)
	{
		HPR_ASSERT_MSG(divisor > 0, "[voxelfield] divisor <= 0");

		if (value >= 0)
			return value / divisor;

		return -(((-value) + divisor - 1) / divisor);
	}

	[[nodiscard]] static VoxelChunkCoord chunk_of(VoxelCoord coord)
	{
		return VoxelChunkCoord {
			floor_div(coord.x, k_chunk_size),
			floor_div(coord.y, k_chunk_size),
			floor_div(coord.z, k_chunk_size)
		};
	}

	[[nodiscard]] static uint64_t key_of(VoxelChunkCoord coord)
	{
		static constexpr uint64_t k_fnv1a_offset_basis = 14695981039346656037ULL;
		static constexpr uint64_t k_fnv1a_prime        = 1099511628211ULL;

		uint64_t fnv1a = k_fnv1a_offset_basis;

		const uint32_t x = static_cast<uint32_t>(coord.chunk_x);
		const uint32_t y = static_cast<uint32_t>(coord.chunk_y);
		const uint32_t z = static_cast<uint32_t>(coord.chunk_z);

		fnv1a ^= x; fnv1a *= k_fnv1a_prime;
		fnv1a ^= y; fnv1a *= k_fnv1a_prime;
		fnv1a ^= z; fnv1a *= k_fnv1a_prime;

		return fnv1a;
	}

	[[nodiscard]] VoxelType get(VoxelCoord coord) const
	{
		const VoxelChunkCoord chunk_coord = chunk_of(coord);
		const uint64_t key = key_of(chunk_coord);

		const VoxelChunk* chunk = find_chunk(key);
		if (!chunk)
			return 0;

		const uint32_t idx = local_index(coord, chunk_coord);
		return chunk->voxels[idx];
	}

	void set(VoxelCoord coord, VoxelType voxel_type)
	{
		const VoxelChunkCoord chunk_coord = chunk_of(coord);
		VoxelChunk& chunk = ensure_chunk(chunk_coord, 0);

		const uint32_t idx = local_index(coord, chunk_coord);
		chunk.voxels[idx] = voxel_type;
	}

	[[nodiscard]] VoxelType* get_ptr(VoxelCoord coord)
	{
		const VoxelChunkCoord chunk_coord = chunk_of(coord);
		const uint64_t key = key_of(chunk_coord);

		VoxelChunk* chunk = find_chunk(key);
		if (!chunk)
			return nullptr;

		const uint32_t idx = local_index(coord, chunk_coord);
		return &chunk->voxels[idx];
	}

	[[nodiscard]] const VoxelType* get_ptr(VoxelCoord coord) const
	{
		const VoxelChunkCoord chunk_coord = chunk_of(coord);
		const uint64_t key = key_of(chunk_coord);

		const VoxelChunk* chunk = find_chunk(key);
		if (!chunk)
			return nullptr;

		const uint32_t idx = local_index(coord, chunk_coord);
		return &chunk->voxels[idx];
	}

private:

	[[nodiscard]] static uint32_t local_index(VoxelCoord coord, VoxelChunkCoord chunk_coord)
	{
		const int32_t base_x = chunk_coord.chunk_x * k_chunk_size;
		const int32_t base_y = chunk_coord.chunk_y * k_chunk_size;
		const int32_t base_z = chunk_coord.chunk_z * k_chunk_size;

		const int32_t local_x = coord.x - base_x;
		const int32_t local_y = coord.y - base_y;
		const int32_t local_z = coord.z - base_z;

		HPR_ASSERT_MSG(local_x >= 0 && local_x < k_chunk_size, "[voxelfield] local_x out of range");
		HPR_ASSERT_MSG(local_y >= 0 && local_y < k_chunk_size, "[voxelfield] local_y out of range");
		HPR_ASSERT_MSG(local_z >= 0 && local_z < k_chunk_size, "[voxelfield] local_z out of range");

		const uint32_t x = static_cast<uint32_t>(local_x);
		const uint32_t y = static_cast<uint32_t>(local_y);
		const uint32_t z = static_cast<uint32_t>(local_z);

		const uint32_t s = static_cast<uint32_t>(k_chunk_size);
		return x + z * s + y * s * s;
	}

	[[nodiscard]] VoxelChunk* find_chunk(uint64_t key)
	{
		auto it = m_index.find(key);
		if (it == m_index.end())
			return nullptr;

		const uint32_t idx = it->second;
		HPR_ASSERT_MSG(idx < m_chunks.size(), "[voxelfield] index map out of range");
		return &m_chunks[idx];
	}

	[[nodiscard]] const VoxelChunk* find_chunk(uint64_t key) const
	{
		auto it = m_index.find(key);
		if (it == m_index.end())
			return nullptr;

		const uint32_t idx = it->second;
		HPR_ASSERT_MSG(idx < m_chunks.size(), "[voxelfield] index map out of range");
		return &m_chunks[idx];
	}

	VoxelChunk& ensure_chunk(VoxelChunkCoord chunk_coord, VoxelType fill_value)
	{
		const uint64_t key = key_of(chunk_coord);

		if (VoxelChunk* existing = find_chunk(key))
			return *existing;

		VoxelChunk chunk {};
		chunk.coord = chunk_coord;
		chunk.key   = key;

		const uint32_t count =
			static_cast<uint32_t>(k_chunk_size) *
			static_cast<uint32_t>(k_chunk_size) *
			static_cast<uint32_t>(k_chunk_size);

		chunk.voxels.reserve(count);

		for (uint32_t i = 0; i < count; ++i)
			chunk.voxels.emplace_back(fill_value);

		const uint32_t new_index = static_cast<uint32_t>(m_chunks.size());
		m_chunks.emplace_back(std::move(chunk));
		m_index[key] = new_index;

		return m_chunks.back();
	}

private:

	mtp::vault<VoxelChunk, mtp::default_set> m_chunks;

	IndexMap m_index {mtp::make_unordered_map<uint64_t, uint32_t, mtp::default_set>()};
};


} // hpr::scn

