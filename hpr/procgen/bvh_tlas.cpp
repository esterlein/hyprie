#include "bvh_tlas.hpp"

#include <cstring>
#include <algorithm>


namespace hpr::geo {


TlasBVH TlasBuilder::build(
	const mtp::vault<vec3,     mtp::default_set>& bound_mins,
	const mtp::vault<vec3,     mtp::default_set>& bound_maxs,
	const mtp::vault<uint32_t, mtp::default_set>& blas_root_idxs,
	const mtp::vault<uint32_t, mtp::default_set>& blas_cnts
)
{
	uint32_t mesh_cnt = static_cast<uint32_t>(blas_root_idxs.size());
	if (mesh_cnt == 0) {
		return TlasBVH {
			.nodes    = {},
			.root_idx = 0xFFFFFFFFU
		};
	}

	m_bvh8_node_cnt = 0;
	m_bvh2_node_cnt = 0;

	/* compute morton keys and partition meshes into coarse spatial buckets */

	SceneBounds scene_bounds =
		compute_scene_bounds(mesh_cnt, bound_mins, bound_maxs);

	extract_morton_keys(mesh_cnt, scene_bounds, bound_mins, bound_maxs);

	uint32_t bucket_mesh_cnts[cfg::morton_bucket_cnt]  = {0};
	uint32_t bucket_start_idxs[cfg::morton_bucket_cnt] = {0};

	for (uint32_t i = 0; i < mesh_cnt; ++i) {
		uint32_t bucket_idx =
			m_raw_mesh_keys[i].morton_code >> cfg::morton_bucket_shift;

		++bucket_mesh_cnts[bucket_idx];
	}

	uint32_t bucket_prefix_sums = 0;
	for (uint32_t i = 0; i < cfg::morton_bucket_cnt; ++i) {
		bucket_start_idxs[i] = bucket_prefix_sums;
		bucket_prefix_sums  += bucket_mesh_cnts[i];
	}

	m_sorted_mesh_keys.resize(mesh_cnt);
	m_temp_mesh_keys.resize(mesh_cnt);

	uint32_t curr_bucket_idxs[cfg::morton_bucket_cnt];
	std::memcpy(curr_bucket_idxs, bucket_start_idxs, sizeof(bucket_start_idxs));

	for (uint32_t i = 0; i < mesh_cnt; ++i) {
		uint32_t bucket_idx =
			m_raw_mesh_keys[i].morton_code >> cfg::morton_bucket_shift;

		m_sorted_mesh_keys[curr_bucket_idxs[bucket_idx]++] = m_raw_mesh_keys[i];
	}

	for (uint32_t i = 0; i < cfg::morton_bucket_cnt; ++i) {
		if (bucket_mesh_cnts[i] > 1) {
			uint32_t bucket_start_idx = bucket_start_idxs[i];

			radix_sort_bucket(
				&m_sorted_mesh_keys[bucket_start_idx],
				&m_temp_mesh_keys[bucket_start_idx],
				bucket_mesh_cnts[i]
			);
		}
	}

	/* emit morton treelets for populated buckets */

	m_bvh8_nodes.resize(mesh_cnt * 4);

	m_treelet_bb_mins.clear();
	m_treelet_bb_maxs.clear();
	m_treelet_centroids.clear();
	m_treelet_roots.clear();
	m_treelet_costs.clear();

	for (uint32_t i = 0; i < cfg::morton_bucket_cnt; ++i) {
		if (bucket_mesh_cnts[i] == 0) {
			continue;
		}

		Treelet treelet = emit_morton_treelet(
			bound_mins,
			bound_maxs,
			blas_root_idxs,
			blas_cnts,
			m_sorted_mesh_keys.data(),
			bucket_start_idxs[i],
			bucket_start_idxs[i] + bucket_mesh_cnts[i],
			18
		);

		m_treelet_roots.push_back(treelet.node_idx);
		m_treelet_bb_mins.push_back(treelet.bound_min);
		m_treelet_bb_maxs.push_back(treelet.bound_max);
		m_treelet_centroids.push_back((treelet.bound_min + treelet.bound_max) * 0.5f);
		m_treelet_costs.push_back(static_cast<float>(bucket_mesh_cnts[i]) * 1.2f);
	}

	/* construct top level sah tree over treelet roots */

	uint32_t treelet_cnt = static_cast<uint32_t>(m_treelet_roots.size());
	m_active_treelet_idxs.resize(treelet_cnt);

	for (uint32_t i = 0; i < treelet_cnt; ++i) {
		m_active_treelet_idxs[i] = i;
	}

	m_bvh2_nodes.resize(treelet_cnt * 2);
	m_bvh2_node_cnt = 1;
	
	build_sah_bvh2_node(m_active_treelet_idxs.data(), 0, treelet_cnt, 0);

	/* collapse binary tree into 8-way nodes */

	uint32_t abs_root_idx = collapse_bvh2_bvh8(0);

	m_bvh8_nodes.resize(m_bvh8_node_cnt);

	return TlasBVH {
		.nodes    = std::move(m_bvh8_nodes),
		.root_idx = abs_root_idx
	};
}


TlasBuilder::SceneBounds TlasBuilder::compute_scene_bounds(
	uint32_t                                  mesh_cnt,
	const mtp::vault<vec3, mtp::default_set>& bound_mins,
	const mtp::vault<vec3, mtp::default_set>& bound_maxs
) const
{
	SceneBounds bounds {
		.min = vec3(std::numeric_limits<float>::max()),
		.max = vec3(std::numeric_limits<float>::lowest())
	};

	for (uint32_t i = 0; i < mesh_cnt; ++i) {
		bounds.min = glm::min(bounds.min, bound_mins[i]);
		bounds.max = glm::max(bounds.max, bound_maxs[i]);
	}

	return bounds;
}


float TlasBuilder::compute_area(const vec3& bound_min, const vec3& bound_max)
{
	vec3 extent = bound_max - bound_min;
	if (extent.x <= 0.0f || extent.y <= 0.0f || extent.z <= 0.0f) {
		return 0.0f;
	}

	return 2.0f * (extent.x * extent.y + extent.y * extent.z + extent.z * extent.x);
}


void TlasBuilder::extract_morton_keys(
	uint32_t                                  mesh_cnt,
	const SceneBounds&                        scene_bounds,
	const mtp::vault<vec3, mtp::default_set>& bound_mins,
	const mtp::vault<vec3, mtp::default_set>& bound_maxs
)
{
	m_raw_mesh_keys.resize(mesh_cnt);

	vec3 scene_extent = scene_bounds.max - scene_bounds.min;
	vec3 inv_extent = vec3(
		(scene_extent.x > cfg::scene_extent_epsilon) ? (1.0f / scene_extent.x) : 0.0f,
		(scene_extent.y > cfg::scene_extent_epsilon) ? (1.0f / scene_extent.y) : 0.0f,
		(scene_extent.z > cfg::scene_extent_epsilon) ? (1.0f / scene_extent.z) : 0.0f
	);

	for (uint32_t i = 0; i < mesh_cnt; ++i) {
		vec3 centroid = (bound_mins[i] + bound_maxs[i]) * 0.5f;
		vec3 pos_norm = (centroid - scene_bounds.min) * inv_extent;

		m_raw_mesh_keys[i] = SortMeshKey {
			.morton_code = compute_morton_code(pos_norm),
			.mesh_idx    = i
		};
	}
}


uint32_t TlasBuilder::compute_morton_code(vec3 pos_norm)
{
	auto expand_bits = [](uint32_t value) -> uint32_t
	{
		value = (value | (value << 16)) & 0x030000FF;
		value = (value | (value <<  8)) & 0x0300F00F;
		value = (value | (value <<  4)) & 0x030C30C3;
		value = (value | (value <<  2)) & 0x09249249;
		return value;
	};

	uint32_t x_grid = static_cast<uint32_t>(std::clamp(pos_norm.x * 1024.0f, 0.0f, 1023.0f));
	uint32_t y_grid = static_cast<uint32_t>(std::clamp(pos_norm.y * 1024.0f, 0.0f, 1023.0f));
	uint32_t z_grid = static_cast<uint32_t>(std::clamp(pos_norm.z * 1024.0f, 0.0f, 1023.0f));

	return expand_bits(x_grid) | (expand_bits(y_grid) << 1) | (expand_bits(z_grid) << 2);
}


void TlasBuilder::radix_sort_bucket(
	SortMeshKey* keys,
	SortMeshKey* temp_keys,
	uint32_t     keys_cnt
)
{
	if (keys_cnt <= 1) {
		return;
	}

	/* lower 11 bits */

	{
		uint32_t histogram_key_cnt[cfg::radix_bin_cap_lower] = {0};
		uint32_t bin_offsets[cfg::radix_bin_cap_lower];

		for (uint32_t i = 0; i < keys_cnt; ++i) {
			uint32_t bin_idx = keys[i].morton_code & 0x7FF;
			histogram_key_cnt[bin_idx]++;
		}

		bin_offsets[0] = 0;
		for (uint32_t i = 1; i < cfg::radix_bin_cap_lower; ++i) {
			bin_offsets[i] = bin_offsets[i - 1] + histogram_key_cnt[i - 1];
		}

		for (uint32_t i = 0; i < keys_cnt; ++i) {
			uint32_t bin_idx = keys[i].morton_code & 0x7FF;
			temp_keys[bin_offsets[bin_idx]++] = keys[i];
		}
	}

	/* upper 10 bits */

	{
		uint32_t histogram_key_cnt[cfg::radix_bin_cap_upper] = {0};
		uint32_t bin_offsets[cfg::radix_bin_cap_upper];

		for (uint32_t i = 0; i < keys_cnt; ++i) {
			uint32_t bin_idx = (temp_keys[i].morton_code >> 11) & 0x3FF;
			histogram_key_cnt[bin_idx]++;
		}

		bin_offsets[0] = 0;
		for (uint32_t i = 1; i < cfg::radix_bin_cap_upper; ++i) {
			bin_offsets[i] = bin_offsets[i - 1] + histogram_key_cnt[i - 1];
		}

		for (uint32_t i = 0; i < keys_cnt; ++i) {
			uint32_t bin_idx = (temp_keys[i].morton_code >> 11) & 0x3FF;
			keys[bin_offsets[bin_idx]++] = temp_keys[i];
		}
	}
}


TlasBuilder::Treelet TlasBuilder::emit_morton_treelet(
	const mtp::vault<vec3,     mtp::default_set>& bound_mins,
	const mtp::vault<vec3,     mtp::default_set>& bound_maxs,
	const mtp::vault<uint32_t, mtp::default_set>& blas_offs,
	const mtp::vault<uint32_t, mtp::default_set>& blas_cnts,
	const SortMeshKey*                            mesh_keys,
	uint32_t                                      start_key_idx,
	uint32_t                                      end_key_idx,
	int32_t                                       bit_shift
)
{
	uint32_t mesh_cnt = end_key_idx - start_key_idx;

	Treelet treelet;
	treelet.bound_min = vec3(std::numeric_limits<float>::max());
	treelet.bound_max = vec3(std::numeric_limits<float>::lowest());

	auto init_empty_lane = [](TLBVH8Node& node, uint32_t lane)
	{
		node.x_min[lane] = std::numeric_limits<float>::max();
		node.y_min[lane] = std::numeric_limits<float>::max();
		node.z_min[lane] = std::numeric_limits<float>::max();
		node.x_max[lane] = std::numeric_limits<float>::lowest();
		node.y_max[lane] = std::numeric_limits<float>::lowest();
		node.z_max[lane] = std::numeric_limits<float>::lowest();
		node.blas_first[lane] = 0;
		node.blas_count[lane] = 0;
	};

	/* emit leaf */

	if (mesh_cnt <= 8) {
		treelet.node_idx = m_bvh8_node_cnt++;
		TLBVH8Node& leaf_node = m_bvh8_nodes[treelet.node_idx];

		for (uint32_t lane = 0; lane < 8; ++lane) {
			uint32_t key_idx = start_key_idx + lane;

			if (lane < mesh_cnt && key_idx < end_key_idx) {
				uint32_t mesh_idx = mesh_keys[key_idx].mesh_idx;

				leaf_node.x_min[lane] = bound_mins[mesh_idx].x;
				leaf_node.y_min[lane] = bound_mins[mesh_idx].y;
				leaf_node.z_min[lane] = bound_mins[mesh_idx].z;
				leaf_node.x_max[lane] = bound_maxs[mesh_idx].x;
				leaf_node.y_max[lane] = bound_maxs[mesh_idx].y;
				leaf_node.z_max[lane] = bound_maxs[mesh_idx].z;

				leaf_node.blas_first[lane] = blas_offs[mesh_idx];
				leaf_node.blas_count[lane] = blas_cnts[mesh_idx];

				treelet.bound_min = glm::min(
					treelet.bound_min,
					vec3(
						leaf_node.x_min[lane],
						leaf_node.y_min[lane],
						leaf_node.z_min[lane]
					)
				);
				treelet.bound_max = glm::max(
					treelet.bound_max,
					vec3(
						leaf_node.x_max[lane],
						leaf_node.y_max[lane],
						leaf_node.z_max[lane]
					)
				);
			}
			else {
				init_empty_lane(leaf_node, lane);
			}
		}

		return treelet;
	}

	/* find mesh key spans for 8 possible octant lanes */

	uint32_t lane_key_offs[9];

	while (bit_shift >= 0) {
		lane_key_offs[0] = start_key_idx;
		uint32_t key_idx = start_key_idx;

		for (uint32_t lane = 0; lane < 7U; ++lane) {
			if (key_idx == end_key_idx) {
				lane_key_offs[lane + 1] = key_idx;
				continue;
			}

			auto it_split = std::upper_bound(
				mesh_keys + key_idx,
				mesh_keys + end_key_idx,
				lane,
				[bit_shift](uint32_t lane_threshold, const SortMeshKey& mesh_key)
				{
					uint32_t lane_code = (mesh_key.morton_code >> bit_shift) & 7U;
					return lane_threshold < lane_code;
				}
			);

			key_idx = static_cast<uint32_t>(std::distance(mesh_keys, it_split));
			lane_key_offs[lane + 1] = key_idx;
		}
		lane_key_offs[8] = end_key_idx;

		uint32_t populated_lanes = 0;
		for (uint32_t lane = 0; lane < 8; ++lane) {
			if (lane_key_offs[lane + 1] > lane_key_offs[lane]) {
				populated_lanes++;
			}
		}

		if (populated_lanes == 1) {
			bit_shift -= 3;
		}
		else {
			break;
		}
	}

	/* linear chunking when morton bits exhaust */

	if (bit_shift < 0) {
		treelet.node_idx = m_bvh8_node_cnt++;
		TLBVH8Node& fallback_node = m_bvh8_nodes[treelet.node_idx];

		uint32_t chunk_size = (mesh_cnt + 7U) / 8U;

		for (uint32_t lane = 0; lane < 8; ++lane) {

			uint32_t chunk_start =
				start_key_idx + std::min(lane * chunk_size, mesh_cnt);
			uint32_t chunk_end =
				start_key_idx + std::min((lane + 1) * chunk_size, mesh_cnt);

			if (chunk_start < chunk_end) {

				Treelet child_res = emit_morton_treelet(
					bound_mins,
					bound_maxs,
					blas_offs,
					blas_cnts,
					mesh_keys,
					chunk_start,
					chunk_end,
					-1
				);

				fallback_node.blas_first[lane] = child_res.node_idx;
				fallback_node.x_min[lane]      = child_res.bound_min.x;
				fallback_node.y_min[lane]      = child_res.bound_min.y;
				fallback_node.z_min[lane]      = child_res.bound_min.z;
				fallback_node.x_max[lane]      = child_res.bound_max.x;
				fallback_node.y_max[lane]      = child_res.bound_max.y;
				fallback_node.z_max[lane]      = child_res.bound_max.z;
				fallback_node.blas_count[lane] = 0;

				treelet.bound_min = glm::min(treelet.bound_min, child_res.bound_min);
				treelet.bound_max = glm::max(treelet.bound_max, child_res.bound_max);
			}
			else {
				init_empty_lane(fallback_node, lane);
			}
		}
		return treelet;
	}

	/* emit interior node */

	treelet.node_idx = m_bvh8_node_cnt++;
	TLBVH8Node& interior_node = m_bvh8_nodes[treelet.node_idx];

	for (uint32_t lane = 0; lane < 8; ++lane) {
		uint32_t child_start = lane_key_offs[lane];
		uint32_t child_end   = lane_key_offs[lane + 1];

		if (child_end > child_start) {

			Treelet child_res = emit_morton_treelet(
				bound_mins,
				bound_maxs,
				blas_offs,
				blas_cnts,
				mesh_keys,
				child_start,
				child_end,
				bit_shift - 3
			);

			interior_node.blas_first[lane] = child_res.node_idx;
			interior_node.x_min[lane]      = child_res.bound_min.x;
			interior_node.y_min[lane]      = child_res.bound_min.y;
			interior_node.z_min[lane]      = child_res.bound_min.z;
			interior_node.x_max[lane]      = child_res.bound_max.x;
			interior_node.y_max[lane]      = child_res.bound_max.y;
			interior_node.z_max[lane]      = child_res.bound_max.z;
			interior_node.blas_count[lane] = 0;

			treelet.bound_min = glm::min(treelet.bound_min, child_res.bound_min);
			treelet.bound_max = glm::max(treelet.bound_max, child_res.bound_max);
		}
		else {
			init_empty_lane(interior_node, lane);
		}
	}

	return treelet;
}


void TlasBuilder::build_sah_bvh2_node(
	uint32_t* active_treelet_idxs,
	uint32_t  start_treelet_idx,
	uint32_t  end_treelet_idx,
	uint32_t  node_idx
)
{
	uint32_t treelet_cnt = end_treelet_idx - start_treelet_idx;

	/* compute cluster aabbs */

	vec3 cluster_bb_min(std::numeric_limits<float>::max());
	vec3 cluster_bb_max(std::numeric_limits<float>::lowest());

	float leaf_thresh_cost = 0.0f;

	for (uint32_t i = start_treelet_idx; i < end_treelet_idx; ++i) {
		uint32_t treelet_idx = active_treelet_idxs[i];

		cluster_bb_min = glm::min(cluster_bb_min, m_treelet_bb_mins[treelet_idx]);
		cluster_bb_max = glm::max(cluster_bb_max, m_treelet_bb_maxs[treelet_idx]);

		leaf_thresh_cost += m_treelet_costs[treelet_idx];
	}

	/* evaluate split heuristics and fallback to leaf if worse */

	if (treelet_cnt <= 8) {
		m_bvh2_nodes[node_idx].min       = cluster_bb_min;
		m_bvh2_nodes[node_idx].max       = cluster_bb_max;
		m_bvh2_nodes[node_idx].child_idx = start_treelet_idx;
		m_bvh2_nodes[node_idx].mesh_cnt  = treelet_cnt;
		return;
	}

	SahSplit sah_split = eval_sah_split(
		start_treelet_idx,
		treelet_cnt,
		cluster_bb_min,
		cluster_bb_max,
		active_treelet_idxs
	);

	if (!sah_split.is_valid || sah_split.cost >= leaf_thresh_cost) {
		m_bvh2_nodes[node_idx].min       = cluster_bb_min;
		m_bvh2_nodes[node_idx].max       = cluster_bb_max;
		m_bvh2_nodes[node_idx].child_idx = start_treelet_idx;
		m_bvh2_nodes[node_idx].mesh_cnt  = treelet_cnt;
		return;
	}

	/* partition treelets & allocate child nodes */

	uint32_t left_node_treelet_cnt = partition_treelets(
		start_treelet_idx,
		treelet_cnt,
		cluster_bb_min,
		cluster_bb_max,
		sah_split.axis,
		sah_split.bin,
		active_treelet_idxs
	);

	if (left_node_treelet_cnt == 0 || left_node_treelet_cnt >= treelet_cnt) {
		left_node_treelet_cnt = treelet_cnt / 2;
	}

	uint32_t split_treelet_idx = start_treelet_idx + left_node_treelet_cnt;

	uint32_t left_child_idx = m_bvh2_node_cnt;
	m_bvh2_node_cnt += 2;

	/* recurse into children & store non-leaf */

	build_sah_bvh2_node(active_treelet_idxs, start_treelet_idx, split_treelet_idx, left_child_idx);
	build_sah_bvh2_node(active_treelet_idxs, split_treelet_idx, end_treelet_idx,   left_child_idx + 1);

	m_bvh2_nodes[node_idx].min       = cluster_bb_min;
	m_bvh2_nodes[node_idx].max       = cluster_bb_max;
	m_bvh2_nodes[node_idx].child_idx = left_child_idx;
	m_bvh2_nodes[node_idx].mesh_cnt  = 0;
}


TlasBuilder::SahSplit TlasBuilder::eval_sah_split(
	uint32_t        start_treelet_idx,
	uint32_t        treelet_cnt,
	const vec3&     cluster_bb_min,
	const vec3&     cluster_bb_max,
	const uint32_t* active_treelet_idxs
) const
{
	float cluster_bb_area = compute_area(cluster_bb_min, cluster_bb_max);

	if (cluster_bb_area <= 0.0f) {
		return SahSplit {};
	}

	SahSplit sah_split;

	/* evaluate split cost per axis */

	for (int32_t axis_idx = 0; axis_idx < 3; ++axis_idx) {

		float cluster_bb_extent =
			cluster_bb_max[axis_idx] - cluster_bb_min[axis_idx];

		if (cluster_bb_extent < cfg::cluster_extent_epsilon) {
			continue;
		}

		SahTreeletBin sah_bins[cfg::tlas_sah_bins_num];

		float inv_bin_size =
			static_cast<float>(cfg::tlas_sah_bins_num) / cluster_bb_extent;

		/* bin treelets by centroids */

		for (uint32_t i = 0; i < treelet_cnt; ++i) {
			uint32_t treelet_idx = active_treelet_idxs[start_treelet_idx + i];

			float dist_from_min =
				m_treelet_centroids[treelet_idx][axis_idx] - cluster_bb_min[axis_idx];

			int32_t bin_idx = static_cast<int32_t>(dist_from_min * inv_bin_size);
			bin_idx = std::clamp(bin_idx, 0, cfg::tlas_sah_bins_num - 1);

			sah_bins[bin_idx].extend(
				m_treelet_bb_mins[treelet_idx],
				m_treelet_bb_maxs[treelet_idx],
				m_treelet_costs[treelet_idx]
			);
		}

		/* accumulate right-side bounds and treelet costs as suffix sum */

		vec3 right_bb_mins[cfg::tlas_sah_bins_num];
		vec3 right_bb_maxs[cfg::tlas_sah_bins_num];
		float right_costs[cfg::tlas_sah_bins_num];

		vec3 acc_right_bb_min   = vec3(std::numeric_limits<float>::max());
		vec3 acc_right_bb_max   = vec3(std::numeric_limits<float>::lowest());
		float acc_right_cost    = 0.0f;

		for (int32_t bin_idx = cfg::tlas_sah_bins_num - 1; bin_idx >= 0; --bin_idx) {
			if (sah_bins[bin_idx].accum_cost > 0.0f) {
				acc_right_bb_min = glm::min(acc_right_bb_min, sah_bins[bin_idx].bounds_min);
				acc_right_bb_max = glm::max(acc_right_bb_max, sah_bins[bin_idx].bounds_max);
				acc_right_cost  += sah_bins[bin_idx].accum_cost;
			}
			right_bb_mins[bin_idx] = acc_right_bb_min;
			right_bb_maxs[bin_idx] = acc_right_bb_max;
			right_costs[bin_idx]   = acc_right_cost;
		}

		/* iterate left-right and track minimum sah cost */

		vec3 acc_left_bb_min = vec3(std::numeric_limits<float>::max());
		vec3 acc_left_bb_max = vec3(std::numeric_limits<float>::lowest());
		float acc_left_cost  = 0.0f;

		for (int32_t bin_idx = 0; bin_idx < cfg::tlas_sah_bins_num - 1; ++bin_idx) {
			if (sah_bins[bin_idx].accum_cost > 0.0f) {
				acc_left_bb_min = glm::min(acc_left_bb_min, sah_bins[bin_idx].bounds_min);
				acc_left_bb_max = glm::max(acc_left_bb_max, sah_bins[bin_idx].bounds_max);
				acc_left_cost  += sah_bins[bin_idx].accum_cost;
			}

			float cost_left  = acc_left_cost;
			float cost_right = right_costs[bin_idx + 1];

			if (cost_left == 0.0f || cost_right == 0.0f) {
				continue;
			}

			float area_left  = compute_area(acc_left_bb_min, acc_left_bb_max);
			float area_right = compute_area(
				right_bb_mins[bin_idx + 1],
				right_bb_maxs[bin_idx + 1]
			);

			float split_cost = cfg::sah_split_trav_cost + (
				(area_left  / cluster_bb_area) * cost_left +
				(area_right / cluster_bb_area) * cost_right
			);

			if (split_cost < sah_split.cost) {
				sah_split.cost = split_cost;
				sah_split.axis = axis_idx;
				sah_split.bin  = bin_idx;
			}
		}
	}

	sah_split.is_valid = (sah_split.axis != -1);

	return sah_split;
}


uint32_t TlasBuilder::partition_treelets(
	uint32_t    start_treelet_idx,
	uint32_t    treelet_cnt,
	const vec3& cluster_bb_min,
	const vec3& cluster_bb_max,
	int32_t     split_axis_idx,
	int32_t     split_bin_idx,
	uint32_t*   active_treelet_idxs
)
{
	float bb_extent =
		cluster_bb_max[split_axis_idx] - cluster_bb_min[split_axis_idx];

	float inv_bin_size =
		static_cast<float>(cfg::tlas_sah_bins_num) / bb_extent;

	auto get_bin_idx = [
		this,
		start_treelet_idx,
		split_axis_idx,
		&cluster_bb_min,
		inv_bin_size,
		active_treelet_idxs
	](uint32_t rel_idx) -> int32_t
	{
		uint32_t treelet_idx =
			active_treelet_idxs[start_treelet_idx + rel_idx];

		float dist_from_min =
			m_treelet_centroids[treelet_idx][split_axis_idx] - cluster_bb_min[split_axis_idx];

		int32_t bin_idx = static_cast<int32_t>(dist_from_min * inv_bin_size);
		return std::clamp(bin_idx, 0, static_cast<int32_t>(cfg::tlas_sah_bins_num) - 1);
	};

	/* partition treelet indices in-place by axis distance from min */

	int32_t left_idx  = 0;
	int32_t right_idx = static_cast<int32_t>(treelet_cnt) - 1;

	while (left_idx <= right_idx) {
		while (left_idx <= right_idx) {
			if (get_bin_idx(static_cast<uint32_t>(left_idx)) > split_bin_idx) {
				break;
			}
			++left_idx;
		}

		while (left_idx <= right_idx) {
			if (get_bin_idx(static_cast<uint32_t>(right_idx)) <= split_bin_idx) {
				break;
			}
			--right_idx;
		}

		if (left_idx < right_idx) {
			std::swap(
				active_treelet_idxs[start_treelet_idx + static_cast<uint32_t>(left_idx)],
				active_treelet_idxs[start_treelet_idx + static_cast<uint32_t>(right_idx)]
			);

			++left_idx;
			--right_idx;
		}
	}

	return static_cast<uint32_t>(left_idx);
}


uint32_t TlasBuilder::collapse_bvh2_bvh8(uint32_t binary_node_idx)
{
	const TLBVH2Node& binary_node = m_bvh2_nodes[binary_node_idx];

	auto init_empty_lane = [](TLBVH8Node& node, uint32_t lane)
	{
		node.x_min[lane] = std::numeric_limits<float>::max();
		node.y_min[lane] = std::numeric_limits<float>::max();
		node.z_min[lane] = std::numeric_limits<float>::max();
		node.x_max[lane] = std::numeric_limits<float>::lowest();
		node.y_max[lane] = std::numeric_limits<float>::lowest();
		node.z_max[lane] = std::numeric_limits<float>::lowest();
		node.blas_first[lane] = 0;
		node.blas_count[lane] = 0;
	};

	/* pack leaf treelets into bvh8 node */

	if (binary_node.mesh_cnt > 0) {
		uint32_t new_idx = m_bvh8_node_cnt++;
		TLBVH8Node& bvh8_leaf = m_bvh8_nodes[new_idx];

		uint32_t start_idx = binary_node.child_idx;
		uint32_t count     = binary_node.mesh_cnt;

		for (uint32_t lane = 0; lane < 8; ++lane) {
			if (lane < count) {
				uint32_t treelet_idx = m_active_treelet_idxs[start_idx + lane];

				bvh8_leaf.x_min[lane] = m_treelet_bb_mins[treelet_idx].x;
				bvh8_leaf.y_min[lane] = m_treelet_bb_mins[treelet_idx].y;
				bvh8_leaf.z_min[lane] = m_treelet_bb_mins[treelet_idx].z;
				bvh8_leaf.x_max[lane] = m_treelet_bb_maxs[treelet_idx].x;
				bvh8_leaf.y_max[lane] = m_treelet_bb_maxs[treelet_idx].y;
				bvh8_leaf.z_max[lane] = m_treelet_bb_maxs[treelet_idx].z;

				bvh8_leaf.blas_first[lane] = m_treelet_roots[treelet_idx];
				bvh8_leaf.blas_count[lane] = 0;
			}
			else {
				init_empty_lane(bvh8_leaf, lane);
			}
		}

		return new_idx;
	}

	/* setup interior nodes collapse */

	struct ChildCandidate
	{
		uint32_t node_idx;
		float    surface_area;
		bool     is_leaf;
	};

	ChildCandidate child_candidates[8];
	uint32_t child_cnt = 0;

	auto add_candidate = [this, &child_candidates, &child_cnt](uint32_t local_idx)
	{
		const TLBVH2Node& node = m_bvh2_nodes[local_idx];
		child_candidates[child_cnt++] = {
			local_idx,
			compute_area(node.min, node.max),
			node.mesh_cnt > 0
		};
	};

	add_candidate(binary_node.child_idx);
	add_candidate(binary_node.child_idx + 1);

	/* flatten bvh2 nodes into bvh8 simd lanes with greedy sah split */

	while (child_cnt < 8) {
		int32_t best_idx   = -1;
		float   best_score = -1.0f;

		for (uint32_t curr_idx = 0; curr_idx < child_cnt; ++curr_idx) {
			if (child_candidates[curr_idx].is_leaf) {
				continue;
			}

			const TLBVH2Node& node_curr =
				m_bvh2_nodes[child_candidates[curr_idx].node_idx];

			float total_overlap_area = 0.0f;
			for (uint32_t peer_idx = 0; peer_idx < child_cnt; ++peer_idx) {

				if (curr_idx == peer_idx) {
					continue;
				}

				const TLBVH2Node& node_peer =
					m_bvh2_nodes[child_candidates[peer_idx].node_idx];

				vec3 intersect_min = glm::max(node_curr.min, node_peer.min);
				vec3 intersect_max = glm::min(node_curr.max, node_peer.max);
				total_overlap_area += compute_area(intersect_min, intersect_max);
			}

			float score =
				child_candidates[curr_idx].surface_area /
				(1.0f + cfg::area_overlap_weight * total_overlap_area);

			if (score > best_score) {
				best_score = score;
				best_idx   = static_cast<int32_t>(curr_idx);
			}
		}

		if (best_idx == -1) {
			break;
		}

		uint32_t split_node_idx = child_candidates[best_idx].node_idx;

		child_candidates[best_idx] = child_candidates[child_cnt - 1];

		--child_cnt;

		const TLBVH2Node& split_node = m_bvh2_nodes[split_node_idx];

		add_candidate(split_node.child_idx);
		add_candidate(split_node.child_idx + 1);
	}

	uint32_t new_child_idxs[8];
	for (uint32_t i = 0; i < child_cnt; ++i) {
		new_child_idxs[i] = collapse_bvh2_bvh8(child_candidates[i].node_idx);
	}

	/* encode flattened nodes into simd lanes */

	uint32_t new_bvh8_idx = m_bvh8_node_cnt++;
	TLBVH8Node& bvh8_node = m_bvh8_nodes[new_bvh8_idx];

	for (uint32_t lane = 0; lane < 8; ++lane) {
		if (lane < child_cnt) {
			const TLBVH2Node& child = m_bvh2_nodes[child_candidates[lane].node_idx];

			bvh8_node.x_min[lane] = child.min.x;
			bvh8_node.y_min[lane] = child.min.y;
			bvh8_node.z_min[lane] = child.min.z;
			bvh8_node.x_max[lane] = child.max.x;
			bvh8_node.y_max[lane] = child.max.y;
			bvh8_node.z_max[lane] = child.max.z;

			bvh8_node.blas_first[lane] = new_child_idxs[lane];
			bvh8_node.blas_count[lane] = 0;
		}
		else {
			init_empty_lane(bvh8_node, lane);
		}
	}

	return new_bvh8_idx;
}


} // hpr::geo
