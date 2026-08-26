#pragma once

#include "hprint.hpp"
#include "math.hpp"
#include "mtp_memory.hpp"

#include "bvh_data.hpp"

#include <limits>


namespace hpr::geo {


namespace cfg {

inline constexpr uint32_t radix_bin_cap_lower = 2048U;
inline constexpr uint32_t radix_bin_cap_upper = 1024U;

inline constexpr int32_t tlas_sah_bins_num = 16;

inline constexpr float sah_split_trav_cost = 1.0f;
inline constexpr float area_overlap_weight = 1.0f;

inline constexpr float scene_extent_epsilon   = 1e-5f;
inline constexpr float cluster_extent_epsilon = 1e-4f;

inline constexpr uint32_t morton_bucket_cnt   = 512U;
inline constexpr uint32_t morton_code_bits    = 30U;
inline constexpr uint32_t morton_bucket_bits  = 9U;
inline constexpr uint32_t morton_bucket_shift = morton_code_bits - morton_bucket_bits;

} // hpr::geo::cfg


struct TlasBVH
{
	mtp::vault<TLBVH8Node, mtp::default_set> nodes;

	uint32_t root_idx;
};


class TlasBuilder
{
public:

	TlasBuilder()
		: m_bvh8_node_cnt {0}
		, m_bvh2_node_cnt {0}
	{}

	[[nodiscard]] TlasBVH build(
		const mtp::vault<vec3,     mtp::default_set>& input_mins,
		const mtp::vault<vec3,     mtp::default_set>& input_maxs,
		const mtp::vault<uint32_t, mtp::default_set>& input_offs,
		const mtp::vault<uint32_t, mtp::default_set>& input_cnts
	);

private:

	struct SceneBounds
	{
		vec3 min;
		vec3 max;
	};

	struct SortMeshKey
	{
		uint32_t morton_code;
		uint32_t mesh_idx;
	};

	struct SahTreeletBin
	{
		vec3  bounds_min {std::numeric_limits<float>::max()};
		vec3  bounds_max {std::numeric_limits<float>::lowest()};
		float accum_cost {0.0f};

		void extend(const vec3& bound_min, const vec3& bound_max, float cost)
		{
			bounds_min  = glm::min(bounds_min, bound_min);
			bounds_max  = glm::max(bounds_max, bound_max);
			accum_cost += cost;
		}
	};

	struct SahSplit
	{
		bool is_valid {false};

		int32_t axis {-1};
		int32_t bin  {-1};

		float cost {std::numeric_limits<float>::max()};
	};

	struct Treelet
	{
		uint32_t node_idx;

		vec3 bound_min;
		vec3 bound_max;
	};

	struct TlasBinaryNode
	{
		TLBVH8Node node;

		vec3 bound_min;
		vec3 bound_max;

		bool is_leaf;
	};

private:

	[[nodiscard]] SceneBounds compute_scene_bounds(
		uint32_t                                  mesh_cnt,
		const mtp::vault<vec3, mtp::default_set>& bound_mins,
		const mtp::vault<vec3, mtp::default_set>& bound_maxs
	) const;

	[[nodiscard]] static float compute_area(
		const vec3& bound_min,
		const vec3& bound_max
	);

	void extract_morton_keys(
		uint32_t                                  mesh_cnt,
		const SceneBounds&                        scene_bounds,
		const mtp::vault<vec3, mtp::default_set>& bound_mins,
		const mtp::vault<vec3, mtp::default_set>& bound_maxs
	);

	[[nodiscard]] static uint32_t compute_morton_code(vec3 norm_pos);

	static void radix_sort_bucket(
		SortMeshKey* keys,
		SortMeshKey* temp_keys,
		uint32_t     keys_cnt
	);

	[[nodiscard]] Treelet emit_morton_treelet(
		const mtp::vault<vec3,     mtp::default_set>& bound_mins,
		const mtp::vault<vec3,     mtp::default_set>& bound_maxs,
		const mtp::vault<uint32_t, mtp::default_set>& blas_offs,
		const mtp::vault<uint32_t, mtp::default_set>& blas_cnts,
		const SortMeshKey*                            keys,
		uint32_t                                      start_key_idx,
		uint32_t                                      end_key_idx,
		int32_t                                       bit_shift
	);

	void build_sah_bvh2_node(
		uint32_t* active_treelet_idxs,
		uint32_t  start_treelet_idx,
		uint32_t  end_treelet_idx,
		uint32_t  node_idx
	);

	[[nodiscard]] SahSplit eval_sah_split(
		uint32_t        start_treelet_idx,
		uint32_t        treelet_cnt,
		const vec3&     cluster_bb_min,
		const vec3&     cluster_bb_max,
		const uint32_t* active_treelet_idxs
	) const;

	[[nodiscard]] uint32_t partition_treelets(
		uint32_t    start_treelet_idx,
		uint32_t    treelet_cnt,
		const vec3& cluster_bb_min,
		const vec3& cluster_bb_max,
		int32_t     split_axis,
		int32_t     split_bin,
		uint32_t*   active_treelet_idxs
	);

	[[nodiscard]] uint32_t collapse_bvh2_bvh8(uint32_t binary_node_idx);

private:

	mtp::vault<SortMeshKey, mtp::default_set> m_raw_mesh_keys;
	mtp::vault<SortMeshKey, mtp::default_set> m_sorted_mesh_keys;
	mtp::vault<SortMeshKey, mtp::default_set> m_temp_mesh_keys;

	mtp::vault<vec3,     mtp::default_set> m_treelet_bb_mins;
	mtp::vault<vec3,     mtp::default_set> m_treelet_bb_maxs;
	mtp::vault<vec3,     mtp::default_set> m_treelet_centroids;
	mtp::vault<uint32_t, mtp::default_set> m_treelet_roots;
	mtp::vault<float,    mtp::default_set> m_treelet_costs;
	mtp::vault<uint32_t, mtp::default_set> m_active_treelet_idxs;

	uint32_t m_bvh8_node_cnt;
	uint32_t m_bvh2_node_cnt;

	mtp::vault<TLBVH8Node, mtp::default_set> m_bvh8_nodes;
	mtp::vault<TLBVH2Node, mtp::default_set> m_bvh2_nodes;
};


} // hpr::geo
