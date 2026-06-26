#pragma once

#include "hprint.hpp"
#include "mtp_memory.hpp"

#include "log.hpp"
#include "math.hpp"

#include "hull_data.hpp"


namespace hpr::geo {


namespace cfg {

	constexpr float convex_epsilon   = 0.05f;
	constexpr float coplanar_epsilon = 1e-5f;

} // hpr::geo::cfg


inline Simplex init_simplex(const vec3* pos_raw, uint32_t vtx_count)
{
	Simplex simplex {};

	if (vtx_count < 4) {
		return simplex;
	}

	uint32_t x_min_idx = 0;
	uint32_t x_max_idx = 0;
	uint32_t y_min_idx = 0;
	uint32_t y_max_idx = 0;
	uint32_t z_min_idx = 0;
	uint32_t z_max_idx = 0;

	for (uint32_t i = 1; i < vtx_count; ++i) {
		const vec3& pos = pos_raw[i];
		if (pos.x < pos_raw[x_min_idx].x) { x_min_idx = i; }
		if (pos.x > pos_raw[x_max_idx].x) { x_max_idx = i; }
		if (pos.y < pos_raw[y_min_idx].y) { y_min_idx = i; }
		if (pos.y > pos_raw[y_max_idx].y) { y_max_idx = i; }
		if (pos.z < pos_raw[z_min_idx].z) { z_min_idx = i; }
		if (pos.z > pos_raw[z_max_idx].z) { z_max_idx = i; }
	}

	uint32_t extreme_idxs[6] = {
		x_min_idx,
		x_max_idx,
		y_min_idx,
		y_max_idx,
		z_min_idx,
		z_max_idx
	};

	float dist_sq_max = -1.0f;
	uint32_t base_beg_idx = 0;
	uint32_t base_end_idx = 0;

	for (int i = 0; i < 6; ++i) {
		for (int j = i + 1; j < 6; ++j) {
			vec3 edge = pos_raw[extreme_idxs[i]] - pos_raw[extreme_idxs[j]];
			float dist_sq = glm::dot(edge, edge);

			if (dist_sq > dist_sq_max) {
				dist_sq_max  = dist_sq;
				base_beg_idx = extreme_idxs[i];
				base_end_idx = extreme_idxs[j];
			}
		}
	}

	if (dist_sq_max < (cfg::convex_epsilon * cfg::convex_epsilon)) {
		HPR_ERROR(log::LogCategory::procgen,
			"[init_simplex] degenerate submesh space");
		return simplex;
	}

	vec3 baseline = pos_raw[base_end_idx] - pos_raw[base_beg_idx];
	float area_sq_max = -1.0f;
	uint32_t trig_peak_idx = 0;

	for (uint32_t idx = 0; idx < vtx_count; ++idx) {
		vec3 rel_pos  = pos_raw[idx] - pos_raw[base_beg_idx];
		vec3 area     = glm::cross(baseline, rel_pos);
		float area_sq = glm::dot(area, area); 

		if (area_sq > area_sq_max) {
			area_sq_max   = area_sq;
			trig_peak_idx = idx;
		}
	}

	if (area_sq_max < (cfg::convex_epsilon * cfg::convex_epsilon)) {
		HPR_ERROR(log::LogCategory::procgen,
			"[init_simplex] degenerate submesh forms a line");
		return simplex;
	}

	vec3 pln_un_normal = glm::cross(
		baseline,
		pos_raw[trig_peak_idx] - pos_raw[base_beg_idx]
	);

	float vol_abs_max = -1.0f;
	uint32_t tetrahd_peak_idx = 0;

	for (uint32_t idx = 0; idx < vtx_count; ++idx) {
		vec3 rel_pos  = pos_raw[idx] - pos_raw[base_beg_idx];
		float vol_abs = glm::abs(glm::dot(pln_un_normal, rel_pos)); 

		if (vol_abs > vol_abs_max) {
			vol_abs_max      = vol_abs;
			tetrahd_peak_idx = idx;
		}
	}

	float tetrahd_peak_dist = vol_abs_max / glm::length(pln_un_normal);

	if (tetrahd_peak_dist < cfg::convex_epsilon) {
		HPR_ERROR(log::LogCategory::procgen,
			"[init_simplex] degenerate submesh forms a plane");
		return simplex;
	}

	simplex.indices[0] = base_beg_idx;
	simplex.indices[1] = base_end_idx;
	simplex.indices[2] = trig_peak_idx;
	simplex.indices[3] = tetrahd_peak_idx;

	return simplex;
}


inline HullDraft init_hull_draft(const vec3* pos_raw, const Simplex& simplex)
{
	HullDraft draft {};

	vec3 center = (
		pos_raw[simplex.indices[0]] +
		pos_raw[simplex.indices[1]] +
		pos_raw[simplex.indices[2]] +
		pos_raw[simplex.indices[3]]
	) * 0.25f;

	uint32_t init_indices[4][3] = {
		{0, 1, 2},
		{0, 2, 3},
		{0, 3, 1},
		{1, 3, 2}
	};

	/* winding swap and face topology hardwiring */

	for (uint32_t face_idx = 0; face_idx < 4; ++face_idx) {

		uint32_t idx_0 = simplex.indices[init_indices[face_idx][0]];
		uint32_t idx_1 = simplex.indices[init_indices[face_idx][1]];
		uint32_t idx_2 = simplex.indices[init_indices[face_idx][2]];

		vec3 vtx_0 = pos_raw[idx_0];
		vec3 vtx_1 = pos_raw[idx_1];
		vec3 vtx_2 = pos_raw[idx_2];

		vec3 normal = glm::normalize(glm::cross(vtx_1 - vtx_0, vtx_2 - vtx_0));

		if (glm::dot(normal, center - vtx_0) > 0.0f) {
			uint32_t idx_tmp = idx_1;

			idx_1  = idx_2;
			idx_2  = idx_tmp;
			normal = -normal;
		}

		uint32_t base_edge_idx = face_idx * 3;

		HullFace hull_face;
		hull_face.normal        = normal;
		hull_face.plane_dist    = glm::dot(normal, pos_raw[idx_0]);
		hull_face.base_edge_idx = base_edge_idx;

		draft.faces.push_back(hull_face);

		draft.half_edges.push_back({idx_0, 0xFFFFFFFF, face_idx, base_edge_idx + 1});
		draft.half_edges.push_back({idx_1, 0xFFFFFFFF, face_idx, base_edge_idx + 2});
		draft.half_edges.push_back({idx_2, 0xFFFFFFFF, face_idx, base_edge_idx + 0});
	}

	/* link twins */

	for (uint32_t i = 0; i < 12; ++i) {
		uint32_t origin_A = draft.half_edges[i].origin;
		uint32_t target_A = draft.half_edges[draft.half_edges[i].next].origin;

		for (uint32_t j = 0; j < 12; ++j) {

			if (i == j)
				continue;

			uint32_t origin_B = draft.half_edges[j].origin;
			uint32_t target_B = draft.half_edges[draft.half_edges[j].next].origin;

			if (origin_A == target_B && target_A == origin_B) {
				draft.half_edges[i].twin = j;
				break;
			}
		}
	}

	return draft;
}


inline HullRaw compute_convex_hull(const vec3* positions_raw, uint32_t prim_vtx_count)
{
	Simplex simplex = init_simplex(positions_raw, prim_vtx_count);

	if (simplex.indices[0] == 0xFFFFFFFF) {
		HPR_ERROR(log::LogCategory::procgen,
			"[compute_convex_hull] degenerate initial simplex");
		return HullRaw {};
	}

	HullDraft draft = init_hull_draft(positions_raw, simplex);

	/* partition positions to face conflict lists */

	for (uint32_t vtx_idx = 0; vtx_idx < prim_vtx_count; ++vtx_idx) {
		if (vtx_idx == simplex.indices[0] || vtx_idx == simplex.indices[1] ||
			vtx_idx == simplex.indices[2] || vtx_idx == simplex.indices[3]) {
			continue;
		}

		vec3 pos = positions_raw[vtx_idx];
		float max_dist = cfg::coplanar_epsilon;
		uint32_t face_best = 0xFFFFFFFF;

		for (uint32_t face_idx = 0; face_idx < 4; ++face_idx) {

			float dist =
				glm::dot(draft.faces[face_idx].normal, pos) - draft.faces[face_idx].plane_dist;

			if (dist > max_dist) {
				max_dist  = dist;
				face_best = face_idx;
			}
		}

		if (face_best != 0xFFFFFFFF) {
			uint32_t node_idx = static_cast<uint32_t>(draft.conflict_nodes.size());
			draft.conflict_nodes.push_back({vtx_idx, max_dist, draft.faces[face_best].conflict_head});
			draft.faces[face_best].conflict_head = node_idx;
		}
	}

	/* expand hull */

	mtp::vault<uint32_t, mtp::default_set> visible_faces;
	mtp::vault<uint32_t, mtp::default_set> horizon_edges;
	mtp::vault<uint32_t, mtp::default_set> face_stack;

	while (true) {
		float max_dist = -1.0f;
		uint32_t peak_face_idx = 0xFFFFFFFF;
		uint32_t peak_node_idx = 0xFFFFFFFF;

		/* find peak point */

		for (uint32_t face_idx = 0; face_idx < static_cast<uint32_t>(draft.faces.size()); ++face_idx) {
			if (!draft.faces[face_idx].active)
				continue;

			uint32_t node_idx = draft.faces[face_idx].conflict_head;
			while (node_idx != 0xFFFFFFFF) {
				const auto& node = draft.conflict_nodes[node_idx];
				if (node.dist > max_dist) {
					max_dist      = node.dist;
					peak_face_idx = face_idx;
					peak_node_idx = node_idx;
				}
				node_idx = node.next;
			}
		}

		if (peak_face_idx == 0xFFFFFFFF) {
			break;
		}

		uint32_t peak_idx = draft.conflict_nodes[peak_node_idx].vtx_idx;
		vec3 peak_pos = positions_raw[peak_idx];

		/* find visible faces (dfs) */

		visible_faces.clear();
		face_stack.clear();
		
		face_stack.push_back(peak_face_idx);
		draft.faces[peak_face_idx].active = false;

		while (!face_stack.empty()) {
			uint32_t curr_face = face_stack.back();

			face_stack.pop_back();
			visible_faces.push_back(curr_face);

			for (uint32_t offset = 0; offset < 3; ++offset) {
				uint32_t edge_idx      = draft.faces[curr_face].base_edge_idx + offset;
				uint32_t twin_idx      = draft.half_edges[edge_idx].twin;
				uint32_t neighbor_face = draft.half_edges[twin_idx].face;

				if (!draft.faces[neighbor_face].active) {
					continue;
				}

				float dist =
					glm::dot(draft.faces[neighbor_face].normal, peak_pos) - draft.faces[neighbor_face].plane_dist;

				if (dist > cfg::coplanar_epsilon) {
					draft.faces[neighbor_face].active = false;
					face_stack.push_back(neighbor_face);
				}
			}
		}

		/* extract horizon */

		horizon_edges.clear();
		for (uint32_t vis_face : visible_faces) {
			for (uint32_t offset = 0; offset < 3; ++offset) {
				uint32_t edge_idx      = draft.faces[vis_face].base_edge_idx + offset;
				uint32_t twin_idx      = draft.half_edges[edge_idx].twin;
				uint32_t neighbor_face = draft.half_edges[twin_idx].face;

				if (draft.faces[neighbor_face].active) {
					horizon_edges.push_back(twin_idx);
				}
			}
		}

		/* new faces from horizon */

		uint32_t new_faces_start = static_cast<uint32_t>(draft.faces.size());
		uint32_t new_edges_start = static_cast<uint32_t>(draft.half_edges.size());

		mtp::vault<uint32_t, mtp::default_set> orphan_nodes;

		for (uint32_t vis_face : visible_faces) {
			uint32_t node_idx = draft.faces[vis_face].conflict_head;
			while (node_idx != 0xFFFFFFFF) {
				if (node_idx != peak_node_idx) {
					orphan_nodes.push_back(node_idx);
				}
				node_idx = draft.conflict_nodes[node_idx].next;
			}
			draft.faces[vis_face].conflict_head = 0xFFFFFFFF;
		}

		for (uint32_t twin_idx : horizon_edges) {
			uint32_t origin_A = draft.half_edges[twin_idx].origin;
			uint32_t target_A = draft.half_edges[draft.half_edges[twin_idx].next].origin;

			uint32_t idx_0 = target_A;
			uint32_t idx_1 = origin_A;
			uint32_t idx_2 = peak_idx;

			vec3 pos_0 = positions_raw[idx_0];
			vec3 pos_1 = positions_raw[idx_1];
			vec3 pos_2 = positions_raw[idx_2];

			vec3 normal            = glm::normalize(glm::cross(pos_1 - pos_0, pos_2 - pos_0));
			uint32_t base_edge_idx = static_cast<uint32_t>(draft.half_edges.size());
			uint32_t curr_face_idx = static_cast<uint32_t>(draft.faces.size());

			HullFace face {};
			face.normal        = normal;
			face.plane_dist    = glm::dot(normal, pos_0);
			face.base_edge_idx = base_edge_idx;
			face.active        = true;

			draft.faces.push_back(face);

			draft.half_edges.push_back({idx_0, twin_idx,   curr_face_idx, base_edge_idx + 1});
			draft.half_edges.push_back({idx_1, 0xFFFFFFFF, curr_face_idx, base_edge_idx + 2});
			draft.half_edges.push_back({idx_2, 0xFFFFFFFF, curr_face_idx, base_edge_idx + 0});

			draft.half_edges[twin_idx].twin = base_edge_idx;
		}

		/* link new lateral twins */

		for (uint32_t i = new_edges_start; i < static_cast<uint32_t>(draft.half_edges.size()); ++i) {

			if (draft.half_edges[i].twin != 0xFFFFFFFF) {
				continue;
			}

			uint32_t origin_A = draft.half_edges[i].origin;
			uint32_t target_A = draft.half_edges[draft.half_edges[i].next].origin;

			for (uint32_t j = i + 1; j < static_cast<uint32_t>(draft.half_edges.size()); ++j) {

				if (draft.half_edges[j].twin != 0xFFFFFFFF) {
					continue;
				}

				uint32_t origin_B = draft.half_edges[j].origin;
				uint32_t target_B = draft.half_edges[draft.half_edges[j].next].origin;

				if (origin_A == target_B && target_A == origin_B) {
					draft.half_edges[i].twin = j;
					draft.half_edges[j].twin = i;
					break;
				}
			}
		}

		/* repartition orphans */

		for (uint32_t node_idx : orphan_nodes) {

			auto& node = draft.conflict_nodes[node_idx];
			vec3 pos   = positions_raw[node.vtx_idx];

			float max_dist             = cfg::coplanar_epsilon;
			uint32_t farthest_face_idx = 0xFFFFFFFF;
			uint32_t faces_size        = static_cast<uint32_t>(draft.faces.size());

			for (uint32_t face_idx = new_faces_start; face_idx < faces_size; ++face_idx) {

				float dist =
					glm::dot(draft.faces[face_idx].normal, pos) - draft.faces[face_idx].plane_dist;

				if (dist > max_dist) {
					max_dist          = dist;
					farthest_face_idx = face_idx;
				}
			}

			if (farthest_face_idx != 0xFFFFFFFF) {
				node.dist = max_dist;
				node.next = draft.faces[farthest_face_idx].conflict_head;
				draft.faces[farthest_face_idx].conflict_head = node_idx;
			}
		}
	}

	/* final hull */

	HullRaw final_hull {};
	
	mtp::vault<uint32_t, mtp::default_set> global_to_local(prim_vtx_count, 0xFFFFFFFF);

	for (size_t face_idx = 0; face_idx < draft.faces.size(); ++face_idx) {

		if (!draft.faces[face_idx].active) {
			continue;
		}

		uint32_t edge_curr = draft.faces[face_idx].base_edge_idx;

		for (uint32_t vtx_idx = 0; vtx_idx < 3; ++vtx_idx) {
			uint32_t global_idx = draft.half_edges[edge_curr].origin;

			if (global_to_local[global_idx] == 0xFFFFFFFF) {
				global_to_local[global_idx] = static_cast<uint32_t>(final_hull.vertices.size());
				final_hull.vertices.push_back(positions_raw[global_idx]);
			}

			final_hull.indices.push_back(global_to_local[global_idx]);

			edge_curr = draft.half_edges[edge_curr].next;
		}
	}

	return final_hull;
}

} // hpr::geo
