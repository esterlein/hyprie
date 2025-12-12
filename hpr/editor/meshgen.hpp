#pragma once

#include <cstdint>

#include "mtp_memory.hpp"

#include "math.hpp"
#include "editor_data.hpp"


namespace hpr::edt {


GeometryRange append_quad_xy(
	mtp::vault<vec3,     mtp::default_set>& positions,
	mtp::vault<uint32_t, mtp::default_set>& indices,
	float                                   half_extent
);

GeometryRange append_box(
	mtp::vault<vec3,     mtp::default_set>& positions,
	mtp::vault<uint32_t, mtp::default_set>& indices,
	const vec3&                             bounds_min,
	const vec3&                             bounds_max
);

GeometryRange append_ring(
	mtp::vault<vec3,     mtp::default_set>& positions,
	mtp::vault<uint32_t, mtp::default_set>& indices,
	int                                     segment_count,
	float                                   radius,
	float                                   thickness
);

GeometryRange append_cone(
	mtp::vault<vec3,     mtp::default_set>& positions,
	mtp::vault<uint32_t, mtp::default_set>& indices,
	int                                     segment_count,
	float                                   base_radius,
	float                                   base_z,
	float                                   apex_z
);

GeometryRange append_arrow(
	mtp::vault<vec3,     mtp::default_set>& positions,
	mtp::vault<uint32_t, mtp::default_set>& indices
);

void build_gizmo_geometry(
	mtp::vault<vec3,     mtp::default_set>& positions,
	mtp::vault<uint32_t, mtp::default_set>& indices,
	GeometryRange&                          range_arrow,
	GeometryRange&                          range_ring,
	GeometryRange&                          range_quad,
	GeometryRange&                          range_cube,
	const GizmoStyle&                       style,
	int                                     ring_segments,
	int                                     cone_segments
);


} // hpr::edt

