#include "meshgen.hpp"

#include <cmath>

#include "panic.hpp"


namespace hpr::edt {


GeometryRange append_quad_xy(
	mtp::vault<vec3,     mtp::default_set>& positions,
	mtp::vault<uint32_t, mtp::default_set>& indices,
	float                                   half_extent
)
{
	const uint32_t vtx_start_index = static_cast<uint32_t>(positions.size());
	const uint32_t idx_start_index = static_cast<uint32_t>(indices.size());

	positions.emplace_back(vec3 {-half_extent, -half_extent, 0.0f});
	positions.emplace_back(vec3 { half_extent, -half_extent, 0.0f});
	positions.emplace_back(vec3 { half_extent,  half_extent, 0.0f});
	positions.emplace_back(vec3 {-half_extent,  half_extent, 0.0f});

	indices.emplace_back(vtx_start_index + 0);
	indices.emplace_back(vtx_start_index + 1);
	indices.emplace_back(vtx_start_index + 2);
	indices.emplace_back(vtx_start_index + 0);
	indices.emplace_back(vtx_start_index + 2);
	indices.emplace_back(vtx_start_index + 3);

	GeometryRange range {
		.first_idx = idx_start_index,
		.idx_count = 6,
		.first_vtx = vtx_start_index,
		.vtx_count = 4
	};

	return range;
}


GeometryRange append_box(
	mtp::vault<vec3,     mtp::default_set>& positions,
	mtp::vault<uint32_t, mtp::default_set>& indices,
	const vec3&                             bounds_min,
	const vec3&                             bounds_max
)
{
	const uint32_t vtx_start_index = static_cast<uint32_t>(positions.size());
	const uint32_t idx_start_index = static_cast<uint32_t>(indices.size());

	const vec3 corner[8] = {

		{bounds_min.x, bounds_min.y, bounds_min.z},
		{bounds_max.x, bounds_min.y, bounds_min.z},
		{bounds_max.x, bounds_max.y, bounds_min.z},
		{bounds_min.x, bounds_max.y, bounds_min.z},
		{bounds_min.x, bounds_min.y, bounds_max.z},
		{bounds_max.x, bounds_min.y, bounds_max.z},
		{bounds_max.x, bounds_max.y, bounds_max.z},
		{bounds_min.x, bounds_max.y, bounds_max.z}
	};

	const uint32_t face_indices[6][4] = {

		{0,1,2,3},
		{4,5,6,7},
		{0,4,7,3},
		{1,5,6,2},
		{3,2,6,7},
		{0,1,5,4}
	};

	for (int face_index = 0; face_index < 6; ++face_index) {

		const uint32_t face_vertex_start = static_cast<uint32_t>(positions.size());

		positions.emplace_back(corner[face_indices[face_index][0]]);
		positions.emplace_back(corner[face_indices[face_index][1]]);
		positions.emplace_back(corner[face_indices[face_index][2]]);
		positions.emplace_back(corner[face_indices[face_index][3]]);

		indices.emplace_back(face_vertex_start + 0);
		indices.emplace_back(face_vertex_start + 1);
		indices.emplace_back(face_vertex_start + 2);
		indices.emplace_back(face_vertex_start + 0);
		indices.emplace_back(face_vertex_start + 2);
		indices.emplace_back(face_vertex_start + 3);
	}

	GeometryRange range {
		.first_idx = idx_start_index,
		.idx_count = 36,
		.first_vtx = vtx_start_index,
		.vtx_count = 24
	};

	return range;
}


GeometryRange append_ring(
	mtp::vault<vec3,     mtp::default_set>& positions,
	mtp::vault<uint32_t, mtp::default_set>& indices,
	int                                     segment_count,
	float                                   radius,
	float                                   thickness
)
{
	HPR_ASSERT_MSG(segment_count > 0, "[append_ring] segment_count <= 0");

	const float inner_radius = radius - thickness * 0.5f;
	const float outer_radius = radius + thickness * 0.5f;

	const uint32_t vtx_start_index = static_cast<uint32_t>(positions.size());
	const uint32_t idx_start_index = static_cast<uint32_t>(indices.size());

	for (int segment_index = 0; segment_index <= segment_count; ++segment_index) {
		const float angle = (2.0f * glm::pi<float>()) * (static_cast<float>(segment_index) / static_cast<float>(segment_count));

		const float cos_value = std::cos(angle);
		const float sin_value = std::sin(angle);

		positions.emplace_back(vec3 {outer_radius * cos_value, outer_radius * sin_value, 0.0f});
		positions.emplace_back(vec3 {inner_radius * cos_value, inner_radius * sin_value, 0.0f});
	}

	for (int segment_index = 0; segment_index < segment_count; ++segment_index) {
		const uint32_t ring_vtx_offset = vtx_start_index + static_cast<uint32_t>(segment_index * 2);

		indices.emplace_back(ring_vtx_offset + 0);
		indices.emplace_back(ring_vtx_offset + 1);
		indices.emplace_back(ring_vtx_offset + 2);
		indices.emplace_back(ring_vtx_offset + 1);
		indices.emplace_back(ring_vtx_offset + 3);
		indices.emplace_back(ring_vtx_offset + 2);
	}

	GeometryRange range {
		.first_idx = idx_start_index,
		.idx_count = static_cast<uint32_t>(segment_count * 6),
		.first_vtx = vtx_start_index,
		.vtx_count = static_cast<uint32_t>((segment_count + 1) * 2)
	};

	return range;
}


GeometryRange append_ring_solid(
	mtp::vault<vec3,     mtp::default_set>& positions,
	mtp::vault<uint32_t, mtp::default_set>& indices,
	int                                     segment_count,
	float                                   radius,
	float                                   radial_thickness,
	float                                   height
)
{
	HPR_ASSERT_MSG(segment_count > 0, "[append_ring_solid] segment_count <= 0");

	const float inner_radius = radius - radial_thickness * 0.5f;
	const float outer_radius = radius + radial_thickness * 0.5f;
	const float half_height  = height * 0.5f;

	const uint32_t vtx_start_index = static_cast<uint32_t>(positions.size());
	const uint32_t idx_start_index = static_cast<uint32_t>(indices.size());

	for (int segment_index = 0; segment_index <= segment_count; ++segment_index) {
		const float angle = (2.0f * glm::pi<float>()) * (static_cast<float>(segment_index) / static_cast<float>(segment_count));

		const float cos_value = std::cos(angle);
		const float sin_value = std::sin(angle);

		positions.emplace_back(vec3 {outer_radius * cos_value, outer_radius * sin_value, +half_height});
		positions.emplace_back(vec3 {inner_radius * cos_value, inner_radius * sin_value, +half_height});
		positions.emplace_back(vec3 {outer_radius * cos_value, outer_radius * sin_value, -half_height});
		positions.emplace_back(vec3 {inner_radius * cos_value, inner_radius * sin_value, -half_height});
	}

	for (int segment_index = 0; segment_index < segment_count; ++segment_index) {
		const uint32_t base = vtx_start_index + static_cast<uint32_t>(segment_index * 4);
		const uint32_t next = base + 4;

		indices.emplace_back(base + 0);
		indices.emplace_back(base + 1);
		indices.emplace_back(next + 0);
		indices.emplace_back(base + 1);
		indices.emplace_back(next + 1);
		indices.emplace_back(next + 0);

		indices.emplace_back(next + 2);
		indices.emplace_back(base + 3);
		indices.emplace_back(base + 2);
		indices.emplace_back(next + 2);
		indices.emplace_back(next + 3);
		indices.emplace_back(base + 3);

		indices.emplace_back(base + 0);
		indices.emplace_back(base + 2);
		indices.emplace_back(next + 0);
		indices.emplace_back(next + 0);
		indices.emplace_back(base + 2);
		indices.emplace_back(next + 2);

		indices.emplace_back(next + 1);
		indices.emplace_back(base + 3);
		indices.emplace_back(base + 1);
		indices.emplace_back(next + 1);
		indices.emplace_back(next + 3);
		indices.emplace_back(base + 3);
	}

	GeometryRange range {
		.first_idx = idx_start_index,
		.idx_count = static_cast<uint32_t>(segment_count * 24),
		.first_vtx = vtx_start_index,
		.vtx_count = static_cast<uint32_t>((segment_count + 1) * 4)
	};

	return range;
}


GeometryRange append_cone(
	mtp::vault<vec3,     mtp::default_set>& positions,
	mtp::vault<uint32_t, mtp::default_set>& indices,
	int                                     segment_count,
	float                                   base_radius,
	float                                   base_z,
	float                                   apex_z
)
{
	const uint32_t vtx_start_index = static_cast<uint32_t>(positions.size());
	const uint32_t idx_start_index = static_cast<uint32_t>(indices.size());

	const uint32_t apex_vtx_index   = vtx_start_index + static_cast<uint32_t>(segment_count);
	const uint32_t center_vtx_index = apex_vtx_index + 1;

	for (int segment_index = 0; segment_index < segment_count; ++segment_index) {

		const float angle = (2.0f * glm::pi<float>()) * (static_cast<float>(segment_index) / static_cast<float>(segment_count));

		const float cos_value = std::cos(angle);
		const float sin_value = std::sin(angle);

		positions.emplace_back(vec3 {base_radius * cos_value, base_radius * sin_value, base_z});
	}

	positions.emplace_back(vec3 {0.0f, 0.0f, apex_z});
	positions.emplace_back(vec3 {0.0f, 0.0f, base_z});

	for (int segment_index = 0; segment_index < segment_count; ++segment_index) {

		const uint32_t base_vtx_curr = vtx_start_index + static_cast<uint32_t>(segment_index + 0);
		const uint32_t base_vtx_next = vtx_start_index + static_cast<uint32_t>((segment_index + 1) % segment_count);
		
		indices.emplace_back(base_vtx_curr);
		indices.emplace_back(apex_vtx_index);
		indices.emplace_back(base_vtx_next);
	}
	
	for (int segment_index = 0; segment_index < segment_count; ++segment_index) {

		const uint32_t base_vtx_curr = vtx_start_index + static_cast<uint32_t>(segment_index + 0);
		const uint32_t base_vtx_next = vtx_start_index + static_cast<uint32_t>((segment_index + 1) % segment_count);
		
		indices.emplace_back(center_vtx_index);
		indices.emplace_back(base_vtx_next);
		indices.emplace_back(base_vtx_curr);
	}

	GeometryRange range {
		.first_idx = idx_start_index,
		.idx_count = static_cast<uint32_t>(segment_count * 3 + segment_count * 3),
		.first_vtx = vtx_start_index,
		.vtx_count = static_cast<uint32_t>(segment_count + 2)
	};

	return range;
}


GeometryRange append_arrow(
	mtp::vault<vec3,     mtp::default_set>& positions,
	mtp::vault<uint32_t, mtp::default_set>& indices,
	int                                     cone_segments,
	float                                   shaft_length,
	float                                   shaft_radius,
	float                                   tip_length,
	float                                   tip_radius
)
{
	const uint32_t idx_start_index = static_cast<uint32_t>(indices.size());
	const uint32_t vtx_start_index = static_cast<uint32_t>(positions.size());

	const GeometryRange shaft_range =
		append_box(
			positions,
			indices,
			vec3 {-shaft_radius, -shaft_radius, 0.0f},
			vec3 { shaft_radius,  shaft_radius, shaft_length}
		);

	const GeometryRange cone_range =
		append_cone(
			positions,
			indices,
			cone_segments,
			tip_radius,
			shaft_length,
			shaft_length + tip_length
		);

	GeometryRange range {
		.first_idx = idx_start_index,
		.idx_count = shaft_range.idx_count + cone_range.idx_count,
		.first_vtx = vtx_start_index,
		.vtx_count = static_cast<uint32_t>(positions.size()) - vtx_start_index
	};

	return range;
}


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
)
{
	HPR_ASSERT_MSG(style.axis_len_px    > 0.0f, "[build_gizmo_geometry] style.axis_len_px <= 0");
	HPR_ASSERT_MSG(style.ring_radius_px > 0.0f, "[build_gizmo_geometry] style.ring_radius_px <= 0");

	const float axis_len_px       = style.axis_len_px;
	const float shaft_length_norm = glm::max(0.0f, 1.0f - style.cone_len_px / axis_len_px);
	const float tip_length_norm   = glm::min(1.0f, style.cone_len_px / axis_len_px);
	const float shaft_radius_norm = style.axis_thick_px / axis_len_px;
	const float tip_radius_norm   = style.cone_rad_px / axis_len_px;

	range_arrow = append_arrow(
		positions,
		indices,
		cone_segments,
		shaft_length_norm,
		shaft_radius_norm,
		tip_length_norm,
		tip_radius_norm
	);

	const float ring_radius_norm = 1.0f;
	const float ring_thick_norm  = style.ring_thick_px  / style.ring_radius_px;
	const float ring_height_norm = style.ring_height_px / style.ring_radius_px;

	range_ring = append_ring_solid(
		positions,
		indices,
		ring_segments,
		ring_radius_norm,
		ring_thick_norm,
		ring_height_norm
	);

	range_quad = append_quad_xy(positions, indices, 0.5f);
	range_cube = append_box(positions, indices, vec3 {-0.5f, -0.5f, -0.5f}, vec3 {0.5f, 0.5f, 0.5f});
}

} // hpr::edt

