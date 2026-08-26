#include "bvh_blas.hpp"
#include "hprint.hpp"
#include <cstdint>


namespace hpr::geo {


mtp::vault<BLBVH8Node, mtp::default_set> BlasBuilder::build(
	const vec3*     positions,
	const uint32_t* indices,
	uint32_t        vtx_base,
	uint32_t        idx_first,
	uint32_t        idx_count
)
{
	m_bvh2_node_cnt = 1U;
	m_bvh8_node_cnt = 0U;

	uint32_t tri_cnt = idx_count / 3;

	m_tris_mins.resize(tri_cnt);
	m_tris_maxs.resize(tri_cnt);
	m_tris_centroids.resize(tri_cnt);
	m_tris_idxs.resize(tri_cnt);

	/* compute aabbs and centroids for each tri */

	for (uint32_t i = 0; i < tri_cnt; ++i) {
		uint32_t idx_0 = indices[idx_first + i * 3 + 0];
		uint32_t idx_1 = indices[idx_first + i * 3 + 1];
		uint32_t idx_2 = indices[idx_first + i * 3 + 2];

		vec3 vtx_0 = positions[vtx_base + idx_0];
		vec3 vtx_1 = positions[vtx_base + idx_1];
		vec3 vtx_2 = positions[vtx_base + idx_2];

		vec3 bound_min = glm::min(glm::min(vtx_0, vtx_1), vtx_2);
		vec3 bound_max = glm::max(glm::max(vtx_0, vtx_1), vtx_2);

		m_tris_mins[i]      = bound_min;
		m_tris_maxs[i]      = bound_max;
		m_tris_idxs[i]      = i;
		m_tris_centroids[i] = (bound_min + bound_max) * 0.5f;
	}

	/* build binary tree and collapse it into 8-ary tree */

	m_bvh2_nodes.resize(tri_cnt * 2);
	build_sah_bvh2_node(0, 0, tri_cnt);

	m_bvh8_nodes.resize(tri_cnt * 2);
	collapse_bvh2_bvh8(0);
	m_bvh8_nodes.resize(m_bvh8_node_cnt);

	return std::move(m_bvh8_nodes);
}


float BlasBuilder::compute_area(const vec3& bound_min, const vec3& bound_max)
{
	vec3 extent = glm::max(bound_max - bound_min, vec3(0.0f));
	return 2.0f * (extent.x * extent.y + extent.y * extent.z + extent.z * extent.x);
}


float BlasBuilder::compute_intersection_area(
	const BLBVH2Node& node_a,
	const BLBVH2Node& node_b
)
{
	vec3 intersect_min = glm::max(node_a.min, node_b.min);
	vec3 intersect_max = glm::min(node_a.max, node_b.max);
	return compute_area(intersect_min, intersect_max);
}


void BlasBuilder::build_sah_bvh2_node(
	uint32_t node_idx,
	uint32_t start_tri_idx,
	uint32_t end_tri_idx
)
{
	uint32_t tris_cnt = end_tri_idx - start_tri_idx;

	/* compute cluster aabbs */

	vec3 cluster_bb_min(std::numeric_limits<float>::max());
	vec3 cluster_bb_max(std::numeric_limits<float>::lowest());

	for (uint32_t i = start_tri_idx; i < end_tri_idx; ++i) {
		uint32_t tri_idx = m_tris_idxs[i];

		cluster_bb_min = glm::min(cluster_bb_min, m_tris_mins[tri_idx]);
		cluster_bb_max = glm::max(cluster_bb_max, m_tris_maxs[tri_idx]);
	}

	/* evaluate split heuristics and fallback to leaf if worse */

	if (tris_cnt <= 8U) {
		m_bvh2_nodes[node_idx].min       = cluster_bb_min;
		m_bvh2_nodes[node_idx].max       = cluster_bb_max;
		m_bvh2_nodes[node_idx].child_idx = start_tri_idx;
		m_bvh2_nodes[node_idx].tris_cnt  = tris_cnt;
		return;
	}

	SahSplit sah_split = eval_sah_split(
		start_tri_idx,
		tris_cnt,
		cluster_bb_min,
		cluster_bb_max
	);

	if (!sah_split.is_valid || sah_split.cost >= (tris_cnt * cfg::sah_tri_cost)) {
		m_bvh2_nodes[node_idx].min       = cluster_bb_min;
		m_bvh2_nodes[node_idx].max       = cluster_bb_max;
		m_bvh2_nodes[node_idx].child_idx = start_tri_idx;
		m_bvh2_nodes[node_idx].tris_cnt  = tris_cnt;
		return;
	}

	/* partition tris & allocate child nodes */

	uint32_t left_node_tri_cnt = partition_tris(
		start_tri_idx,
		tris_cnt,
		cluster_bb_min,
		cluster_bb_max,
		sah_split.axis,
		sah_split.bin
	);

	if (left_node_tri_cnt == 0 || left_node_tri_cnt >= tris_cnt) {
		left_node_tri_cnt = tris_cnt / 2;
	}

	uint32_t split_tri_idx  = start_tri_idx + left_node_tri_cnt;

	uint32_t left_child_idx = m_bvh2_node_cnt;
	m_bvh2_node_cnt += 2;

	/* recurse into children & store non-leaf */

	build_sah_bvh2_node(left_child_idx,     start_tri_idx, split_tri_idx);
	build_sah_bvh2_node(left_child_idx + 1, split_tri_idx, end_tri_idx);

	m_bvh2_nodes[node_idx].min       = cluster_bb_min;
	m_bvh2_nodes[node_idx].max       = cluster_bb_max;
	m_bvh2_nodes[node_idx].child_idx = left_child_idx;
	m_bvh2_nodes[node_idx].tris_cnt  = 0;
}


BlasBuilder::SahSplit BlasBuilder::eval_sah_split(
	uint32_t    start_tri_idx,
	uint32_t    tris_cnt,
	const vec3& cluster_bb_min,
	const vec3& cluster_bb_max
) const
{
	float cluster_bb_area = compute_area(cluster_bb_min, cluster_bb_max);

	if (cluster_bb_area <= 0.0f) {
		return SahSplit {};
	}

	SahSplit sah_split;

	/* evaluate split cost per axis */

	for (uint32_t axis_idx = 0; axis_idx < 3; ++axis_idx) {

		float cluster_bb_extent =
			cluster_bb_max[axis_idx] - cluster_bb_min[axis_idx];

		if (cluster_bb_extent < cfg::cluster_extent_epsilon) {
			continue;
		}

		SahBin sah_bins[cfg::blas_sah_bins_num];
		float inv_bin_size = static_cast<float>(cfg::blas_sah_bins_num) / cluster_bb_extent;

		/* bin tris by centroids */

		for (uint32_t i = 0; i < tris_cnt; ++i) {
			uint32_t tri_idx = m_tris_idxs[start_tri_idx + i];

			float dist_from_min =
				m_tris_centroids[tri_idx][axis_idx] - cluster_bb_min[axis_idx];

			int32_t bin_idx = static_cast<int32_t>(dist_from_min * inv_bin_size);
			bin_idx = std::clamp(bin_idx, 0, cfg::blas_sah_bins_num - 1);

			sah_bins[bin_idx].extend(m_tris_mins[tri_idx], m_tris_maxs[tri_idx]);
		}

		/* accumulate right-side bounds and tris counts as suffix sum */

		vec3 right_bb_mins[cfg::blas_sah_bins_num];
		vec3 right_bb_maxs[cfg::blas_sah_bins_num];

		vec3 acc_right_bb_min = vec3(std::numeric_limits<float>::max());
		vec3 acc_right_bb_max = vec3(std::numeric_limits<float>::lowest());

		uint32_t right_tris_cnts[cfg::blas_sah_bins_num];
		uint32_t acc_right_tris_cnt = 0;

		for (int32_t bin_idx = cfg::blas_sah_bins_num - 1; bin_idx >= 0; --bin_idx) {

			if (sah_bins[bin_idx].tris_cnt > 0) {
				acc_right_bb_min = glm::min(acc_right_bb_min, sah_bins[bin_idx].min);
				acc_right_bb_max = glm::max(acc_right_bb_max, sah_bins[bin_idx].max);

				acc_right_tris_cnt += sah_bins[bin_idx].tris_cnt;
			}
			right_bb_mins[bin_idx] = acc_right_bb_min;
			right_bb_maxs[bin_idx] = acc_right_bb_max;

			right_tris_cnts[bin_idx] = acc_right_tris_cnt;
		}

		/* iterate left-right and track minimum sah cost */

		vec3 acc_left_bb_min = vec3(std::numeric_limits<float>::max());
		vec3 acc_left_bb_max = vec3(std::numeric_limits<float>::lowest());

		uint32_t acc_left_tris_cnt = 0;

		for (int32_t bin_idx = 0; bin_idx < cfg::blas_sah_bins_num - 1; ++bin_idx) {

			if (sah_bins[bin_idx].tris_cnt > 0) {
				acc_left_bb_min = glm::min(acc_left_bb_min, sah_bins[bin_idx].min);
				acc_left_bb_max = glm::max(acc_left_bb_max, sah_bins[bin_idx].max);

				acc_left_tris_cnt += sah_bins[bin_idx].tris_cnt;
			}

			uint32_t tirs_cnt_left  = acc_left_tris_cnt;
			uint32_t tris_cnt_right = right_tris_cnts[bin_idx + 1];

			if (tirs_cnt_left == 0 || tris_cnt_right == 0) {
				continue;
			}

			float area_left  = compute_area(acc_left_bb_min, acc_left_bb_max);
			float area_right = compute_area(
				right_bb_mins[bin_idx + 1],
				right_bb_maxs[bin_idx + 1]
			);

			float split_cost =
				cfg::sah_node_cost +
				cfg::sah_tri_cost  *
				((tirs_cnt_left * area_left + tris_cnt_right * area_right) / cluster_bb_area);

			if (split_cost < sah_split.cost) {
				sah_split.cost = split_cost;
				sah_split.axis = static_cast<int32_t>(axis_idx);
				sah_split.bin  = bin_idx;
			}
		}
	}

	sah_split.is_valid = (sah_split.axis != -1);

	return sah_split;
}


uint32_t BlasBuilder::partition_tris(
	uint32_t    start_tri_idx,
	uint32_t    tris_cnt,
	const vec3& cluster_bb_min,
	const vec3& cluster_bb_max,
	int32_t     split_axis_idx,
	int32_t     split_bin_idx
)
{
	float bb_extent =
		cluster_bb_max[split_axis_idx] - cluster_bb_min[split_axis_idx];

	float inv_bin_size =
		static_cast<float>(cfg::blas_sah_bins_num) / bb_extent;

	auto get_bin_idx = [
		this,
		start_tri_idx,
		split_axis_idx,
		&cluster_bb_min,
		inv_bin_size
	](uint32_t rel_idx) -> int32_t
	{
		uint32_t tri_idx = m_tris_idxs[start_tri_idx + rel_idx];

		float dist_from_min =
			m_tris_centroids[tri_idx][split_axis_idx] - cluster_bb_min[split_axis_idx];

		int32_t bin_idx =
			static_cast<int32_t>(dist_from_min * inv_bin_size);

		return std::clamp(
			bin_idx,
			0,
			static_cast<int32_t>(cfg::blas_sah_bins_num) - 1
		);
	};

	/* partition tris indices in-place by axis distance from min */

	int32_t left_idx  = 0;
	int32_t right_idx = static_cast<int32_t>(tris_cnt) - 1;

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
				m_tris_idxs[start_tri_idx + static_cast<uint32_t>(left_idx)],
				m_tris_idxs[start_tri_idx + static_cast<uint32_t>(right_idx)]
			);

			++left_idx;
			--right_idx;
		}
	}

	return static_cast<uint32_t>(left_idx);
}


BlasBuilder::QuantizedBounds BlasBuilder::quantize_bounds(
	const vec3& parent_bb_min,
	const vec3& parent_bb_max,
	const vec3& child_bb_min,
	const vec3& child_bb_max
) const
{
	vec3 parent_extent = parent_bb_max - parent_bb_min;

	vec3 inv_extent = vec3(
		(parent_extent.x > cfg::cluster_extent_epsilon)
			? (cfg::quant_scale_8bit / parent_extent.x)
			: 0.0f,
		(parent_extent.y > cfg::cluster_extent_epsilon)
			? (cfg::quant_scale_8bit / parent_extent.y)
			: 0.0f,
		(parent_extent.z > cfg::cluster_extent_epsilon)
			? (cfg::quant_scale_8bit / parent_extent.z)
			: 0.0f
	);

	vec3 norm_min = (child_bb_min - parent_bb_min) * inv_extent;
	vec3 norm_max = (child_bb_max - parent_bb_min) * inv_extent;

	u8vec3 quant_min = u8vec3(
		glm::clamp(glm::floor(norm_min),
		vec3(0.0f),
		vec3(cfg::quant_scale_8bit))
	);
	u8vec3 quant_max = u8vec3(
		glm::clamp(glm::ceil(norm_max),
		vec3(0.0f),
		vec3(cfg::quant_scale_8bit))
	);

	QuantizedBounds quant_bounds {
		.x_min = quant_min.x,
		.y_min = quant_min.y,
		.z_min = quant_min.z,
		.x_max = quant_max.x,
		.y_max = quant_max.y,
		.z_max = quant_max.z
	};

	return quant_bounds;
}


uint32_t BlasBuilder::collapse_bvh2_bvh8(uint32_t bvh2_node_idx)
{
	const BLBVH2Node& bvh2_node = m_bvh2_nodes[bvh2_node_idx];
	uint32_t new_bvh8_idx = m_bvh8_node_cnt++;

	/* quantize and pack leaf tris */

	if (bvh2_node.tris_cnt > 0) {

		BLBVH8Node& bvh8_leaf = m_bvh8_nodes[new_bvh8_idx];

		bvh8_leaf.min  = bvh2_node.min;
		bvh8_leaf.max  = bvh2_node.max;
		bvh8_leaf.pad0 = 0.0f;
		bvh8_leaf.pad1 = 0.0f;

		uint32_t start_tri_idx = bvh2_node.child_idx;

		for (uint32_t lane = 0; lane < 8; ++lane) {
			if (lane < bvh2_node.tris_cnt) {
				uint32_t tri_idx = m_tris_idxs[start_tri_idx + lane];
				
				QuantizedBounds quant_bounds = quantize_bounds(
					bvh8_leaf.min,
					bvh8_leaf.max,
					m_tris_mins[tri_idx],
					m_tris_maxs[tri_idx]
				);

				bvh8_leaf.x_min[lane] = quant_bounds.x_min;
				bvh8_leaf.y_min[lane] = quant_bounds.y_min;
				bvh8_leaf.z_min[lane] = quant_bounds.z_min;
				bvh8_leaf.x_max[lane] = quant_bounds.x_max;
				bvh8_leaf.y_max[lane] = quant_bounds.y_max;
				bvh8_leaf.z_max[lane] = quant_bounds.z_max;

				bvh8_leaf.base_idxs[lane] = m_glob_tris_base + tri_idx;
				bvh8_leaf.tris_cnts[lane] = 1;
			}
			else {
				bvh8_leaf.x_min[lane] = bvh8_leaf.y_min[lane] = bvh8_leaf.z_min[lane] = 255;
				bvh8_leaf.x_max[lane] = bvh8_leaf.y_max[lane] = bvh8_leaf.z_max[lane] = 0;

				bvh8_leaf.base_idxs[lane] = 0;
				bvh8_leaf.tris_cnts[lane] = 0;
			}
		}

		std::memset(bvh8_leaf.pad2, 0, sizeof(bvh8_leaf.pad2));
		return m_glob_node_base + new_bvh8_idx;
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
		const BLBVH2Node& node = m_bvh2_nodes[local_idx];
		child_candidates[child_cnt++] = {
			local_idx,
			compute_area(node.min, node.max),
			node.tris_cnt > 0
		};
	};

	add_candidate(bvh2_node.child_idx);
	add_candidate(bvh2_node.child_idx + 1);

	/* flatten bvh2 nodes into bvh8 simd lanes with greedy sah split */

	while (child_cnt < 8) {
		int32_t best_idx   = -1;
		float   best_score = -1.0f;

		for (uint32_t curr_idx = 0; curr_idx < child_cnt; ++curr_idx) {
			if (child_candidates[curr_idx].is_leaf) {
				continue;
			}

			const BLBVH2Node& node_curr =
				m_bvh2_nodes[child_candidates[curr_idx].node_idx];

			float total_overlap_area = 0.0f;
			for (uint32_t peer_idx = 0; peer_idx < child_cnt; ++peer_idx) {

				if (curr_idx == peer_idx) {
					continue;
				}

				const BLBVH2Node& node_peer =
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

		const BLBVH2Node& split_node = m_bvh2_nodes[split_node_idx];

		add_candidate(split_node.child_idx);
		add_candidate(split_node.child_idx + 1);
	}

	uint32_t new_child_idxs[8];
	for (uint32_t i = 0; i < child_cnt; ++i) {
		new_child_idxs[i] = collapse_bvh2_bvh8(child_candidates[i].node_idx);
	}

	/* encode flattened nodes into 8bit quantized simd lanes */

	BLBVH8Node& bvh8_node = m_bvh8_nodes[new_bvh8_idx];

	bvh8_node.min  = m_bvh2_nodes[bvh2_node_idx].min;
	bvh8_node.max  = m_bvh2_nodes[bvh2_node_idx].max;
	bvh8_node.pad0 = 0.0f;
	bvh8_node.pad1 = 0.0f;

	for (uint32_t lane = 0; lane < 8; ++lane) {
		if (lane < child_cnt) {
			const BLBVH2Node& child = m_bvh2_nodes[child_candidates[lane].node_idx];
			
			QuantizedBounds quant_bounds = quantize_bounds(
				bvh8_node.min,
				bvh8_node.max,
				child.min,
				child.max
			);

			bvh8_node.x_min[lane] = quant_bounds.x_min;
			bvh8_node.y_min[lane] = quant_bounds.y_min;
			bvh8_node.z_min[lane] = quant_bounds.z_min;
			bvh8_node.x_max[lane] = quant_bounds.x_max;
			bvh8_node.y_max[lane] = quant_bounds.y_max;
			bvh8_node.z_max[lane] = quant_bounds.z_max;

			bvh8_node.base_idxs[lane] = new_child_idxs[lane];
			bvh8_node.tris_cnts[lane] = 0;
		}
		else {
			bvh8_node.x_min[lane] = bvh8_node.y_min[lane] = bvh8_node.z_min[lane] = 255;
			bvh8_node.x_max[lane] = bvh8_node.y_max[lane] = bvh8_node.z_max[lane] = 0;

			bvh8_node.base_idxs[lane] = 0;
			bvh8_node.tris_cnts[lane] = 0;
		}
	}

	std::memset(bvh8_node.pad2, 0, sizeof(bvh8_node.pad2));
	return m_glob_node_base + new_bvh8_idx;
}


} // hpr::geo
