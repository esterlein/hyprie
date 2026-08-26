#pragma once

#include "hprint.hpp"
#include "mtp_memory.hpp"

#include <algorithm>


namespace hpr::geo {


inline void extract_unique_edges(
	const uint32_t*                         tris_indices,
	uint32_t                                tris_idx_cnt,
	mtp::vault<uint32_t, mtp::default_set>& line_indices
)
{
	mtp::vault<uint64_t, mtp::default_set> edge_hashes;
	edge_hashes.reserve(tris_idx_cnt);

	for (uint32_t i = 0; i < tris_idx_cnt; i += 3) {

		uint32_t idx_0 = tris_indices[i + 0];
		uint32_t idx_1 = tris_indices[i + 1];
		uint32_t idx_2 = tris_indices[i + 2];

		edge_hashes.push_back((static_cast<uint64_t>(std::min(idx_0, idx_1)) << 32) | std::max(idx_0, idx_1));
		edge_hashes.push_back((static_cast<uint64_t>(std::min(idx_1, idx_2)) << 32) | std::max(idx_1, idx_2));
		edge_hashes.push_back((static_cast<uint64_t>(std::min(idx_2, idx_0)) << 32) | std::max(idx_2, idx_0));
	}

	std::sort(edge_hashes.begin(), edge_hashes.end());
	edge_hashes.erase(std::unique(edge_hashes.begin(), edge_hashes.end()), edge_hashes.end());

	line_indices.reserve(edge_hashes.size() * 2);
	
	for (uint64_t edge : edge_hashes) {
		line_indices.push_back(static_cast<uint32_t>(edge >> 32));
		line_indices.push_back(static_cast<uint32_t>(edge & 0xFFFFFFFF));
	}
}


} // hpr::geo

