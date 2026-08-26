#pragma once

#include "hprint.hpp"
#include "math.hpp"
#include "mtp_memory.hpp"

#include "bvh_data.hpp"

#include <limits>
#include <cstring>
#include <algorithm>


namespace hpr::geo {


namespace cfg {

inline constexpr int32_t blas_sah_bins_num = 16;

inline constexpr float sah_node_cost = 1.0f;
inline constexpr float sah_tri_cost  = 1.2f;

inline constexpr float cluster_extent_epsilon = 1e-5f;
inline constexpr float quant_scale_8bit       = 255.0f;

inline constexpr float area_overlap_weight = 1.0f;

} // hpr::geo::cfg


class BlasBuilder
{
public:

	BlasBuilder(uint32_t glob_tris_base, uint32_t glob_node_base)
		: m_glob_tris_base {glob_tris_base}
		, m_glob_node_base {glob_node_base}
		, m_bvh2_node_cnt  {0}
		, m_bvh8_node_cnt  {0}
	{}

	[[nodiscard]] mtp::vault<BLBVH8Node, mtp::default_set> build(
		const vec3*     positions,
		const uint32_t* indices,
		uint32_t        vtx_base,
		uint32_t        idx_first,
		uint32_t        idx_count
	);

private:

	struct SahBin
	{
		vec3 min {std::numeric_limits<float>::max()};
		vec3 max {std::numeric_limits<float>::lowest()};

		uint32_t tris_cnt {0};

		void extend(const vec3& tri_min, const vec3& tri_max)
		{
			min = glm::min(min, tri_min);
			max = glm::max(max, tri_max);
			tris_cnt++;
		}
	};

	struct SahSplit
	{
		bool is_valid {false};

		int32_t axis {-1};
		int32_t bin  {-1};

		float cost {std::numeric_limits<float>::max()};
	};

	struct QuantizedBounds
	{
		uint8_t x_min;
		uint8_t y_min;
		uint8_t z_min;
		uint8_t x_max;
		uint8_t y_max;
		uint8_t z_max;
	};

private:

	[[nodiscard]] static float compute_area(const vec3& bound_min, const vec3& bound_max);

	[[nodiscard]] static float compute_intersection_area(
		const BLBVH2Node& node_a,
		const BLBVH2Node& node_b
	);

	void build_sah_bvh2_node(uint32_t node_idx, uint32_t start_tri_idx, uint32_t end_tri_idx);

	[[nodiscard]] SahSplit eval_sah_split(
		uint32_t    start_tri_idx,
		uint32_t    tris_cnt,
		const vec3& cluster_bb_min,
		const vec3& cluster_bb_max
	) const;

	[[nodiscard]] uint32_t partition_tris(
		uint32_t    start_tri_idx,
		uint32_t    tris_cnt,
		const vec3& cluster_bb_min,
		const vec3& cluster_bb_max,
		int32_t     split_axis,
		int32_t     split_bin
	);

	[[nodiscard]] QuantizedBounds quantize_bounds(
		const vec3& parent_bb_min,
		const vec3& parent_bb_max,
		const vec3& child_bb_min,
		const vec3& child_bb_max
	) const;

	uint32_t collapse_bvh2_bvh8(uint32_t binary_node_idx);

private:

	mtp::vault<vec3,     mtp::default_set> m_tris_mins;
	mtp::vault<vec3,     mtp::default_set> m_tris_maxs;
	mtp::vault<vec3,     mtp::default_set> m_tris_centroids;
	mtp::vault<uint32_t, mtp::default_set> m_tris_idxs;

	uint32_t m_glob_tris_base;
	uint32_t m_glob_node_base;

	uint32_t m_bvh2_node_cnt;
	uint32_t m_bvh8_node_cnt;

	mtp::vault<BLBVH2Node, mtp::default_set> m_bvh2_nodes;
	mtp::vault<BLBVH8Node, mtp::default_set> m_bvh8_nodes;
};

} // hpr::geo
