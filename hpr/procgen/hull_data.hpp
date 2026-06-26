#pragma once

#include "hprint.hpp"
#include "mtp_memory.hpp"

#include "math.hpp"


namespace hpr::geo {


struct Simplex
{
	uint32_t indices[4] {0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF};
};


struct HullHalfEdge
{
	uint32_t origin;
	uint32_t twin;
	uint32_t face;
	uint32_t next;
};


struct HullFace
{
	vec3     normal;
	float    plane_dist;
	uint32_t base_edge_idx {0xFFFFFFFF};
	uint32_t conflict_head {0xFFFFFFFF};

	bool active {true};
};


struct ConflictNode
{
		uint32_t vtx_idx;
		float    dist;
		uint32_t next;
};


struct HullDraft
{
	mtp::vault<HullHalfEdge, mtp::default_set> half_edges;
	mtp::vault<HullFace,     mtp::default_set> faces;
	mtp::vault<ConflictNode, mtp::default_set> conflict_nodes;

	mtp::vault<uint32_t, mtp::default_set> point_assignments;
	mtp::vault<uint32_t, mtp::default_set> horizon_edges;
	mtp::vault<uint32_t, mtp::default_set> orphaned_points;
	mtp::vault<uint32_t, mtp::default_set> new_faces;
};


struct HullRaw
{
	mtp::vault<vec3,     mtp::default_set> vertices;
	mtp::vault<uint32_t, mtp::default_set> indices;

	bool is_valid {true};
};


} // hpr::geo

