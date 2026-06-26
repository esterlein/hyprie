#include "meshgen.hpp"

#include "panic.hpp"


namespace hpr::geo {


Geoslice Meshgen::quad(const Quad& quad)
{
	const float half_extent = quad.half_extent;

	const uint32_t vtx_base = static_cast<uint32_t>(positions.size());
	const uint32_t idx_base = static_cast<uint32_t>(indices.size());

	positions.emplace_back(vec3 {-half_extent, -half_extent, 0.0f});
	positions.emplace_back(vec3 { half_extent, -half_extent, 0.0f});
	positions.emplace_back(vec3 { half_extent,  half_extent, 0.0f});
	positions.emplace_back(vec3 {-half_extent,  half_extent, 0.0f});

	indices.emplace_back(vtx_base + 0);
	indices.emplace_back(vtx_base + 1);
	indices.emplace_back(vtx_base + 2);
	indices.emplace_back(vtx_base + 0);
	indices.emplace_back(vtx_base + 2);
	indices.emplace_back(vtx_base + 3);

	uvs.emplace_back(vec2 {0.0f, 0.0f});
	uvs.emplace_back(vec2 {1.0f, 0.0f});
	uvs.emplace_back(vec2 {1.0f, 1.0f});
	uvs.emplace_back(vec2 {0.0f, 1.0f});

	return {
		.vtx_base  = vtx_base,
		.vtx_count = 4,
		.idx_first = idx_base,
		.idx_count = 6
	};
}


Geoslice Meshgen::box(const Box& box)
{
	const vec3 bounds_min = box.min;
	const vec3 bounds_max = box.max;

	const uint32_t vtx_base = static_cast<uint32_t>(positions.size());
	const uint32_t idx_base = static_cast<uint32_t>(indices.size());

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

	for (int face_idx = 0; face_idx < 6; ++face_idx) {
		const uint32_t face_vtx_base = static_cast<uint32_t>(positions.size());

		positions.emplace_back(corner[face_indices[face_idx][0]]);
		positions.emplace_back(corner[face_indices[face_idx][1]]);
		positions.emplace_back(corner[face_indices[face_idx][2]]);
		positions.emplace_back(corner[face_indices[face_idx][3]]);

		indices.emplace_back(face_vtx_base + 0);
		indices.emplace_back(face_vtx_base + 1);
		indices.emplace_back(face_vtx_base + 2);
		indices.emplace_back(face_vtx_base + 0);
		indices.emplace_back(face_vtx_base + 2);
		indices.emplace_back(face_vtx_base + 3);

		uvs.emplace_back(vec2 {0.0f, 0.0f});
		uvs.emplace_back(vec2 {1.0f, 0.0f});
		uvs.emplace_back(vec2 {1.0f, 1.0f});
		uvs.emplace_back(vec2 {0.0f, 1.0f});
	}

	return {
		.vtx_base  = vtx_base,
		.vtx_count = 24,
		.idx_first = idx_base,
		.idx_count = 36
	};
}


Geoslice Meshgen::ring(const Ring& ring)
{
	HPR_ASSERT_MSG(ring.segment_count > 0,
		"segment_count <= 0");

	const float inner_radius = ring.radius - ring.thickness * 0.5f;
	const float outer_radius = ring.radius + ring.thickness * 0.5f;

	const uint32_t vtx_base = static_cast<uint32_t>(positions.size());
	const uint32_t idx_base = static_cast<uint32_t>(indices.size());

	for (int segment_idx = 0; segment_idx <= ring.segment_count; ++segment_idx) {
		const float angle = (2.0f * glm::pi<float>()) *
			(static_cast<float>(segment_idx) / static_cast<float>(ring.segment_count));

		const float cos_value = std::cos(angle);
		const float sin_value = std::sin(angle);

		positions.emplace_back(vec3 {outer_radius * cos_value, outer_radius * sin_value, 0.0f});
		positions.emplace_back(vec3 {inner_radius * cos_value, inner_radius * sin_value, 0.0f});

		const float u =
			static_cast<float>(segment_idx) / static_cast<float>(ring.segment_count);
		uvs.emplace_back(vec2 {u, 1.0f});
		uvs.emplace_back(vec2 {u, 0.0f});
	}

	for (int segment_idx = 0; segment_idx < ring.segment_count; ++segment_idx) {
		const uint32_t vtx_offset = vtx_base + static_cast<uint32_t>(segment_idx * 2);

		indices.emplace_back(vtx_offset + 0);
		indices.emplace_back(vtx_offset + 1);
		indices.emplace_back(vtx_offset + 2);
		indices.emplace_back(vtx_offset + 1);
		indices.emplace_back(vtx_offset + 3);
		indices.emplace_back(vtx_offset + 2);
	}

	return {
		.vtx_base  = vtx_base,
		.vtx_count = static_cast<uint32_t>((ring.segment_count + 1) * 2),
		.idx_first = idx_base,
		.idx_count = static_cast<uint32_t>(ring.segment_count * 6),
	};
}


Geoslice Meshgen::ring_solid(const RingSolid& ring)
{
	HPR_ASSERT_MSG(ring.segment_count > 0,
		"segment_count <= 0");

	const float inner_radius = ring.radius - ring.radial_thickness * 0.5f;
	const float outer_radius = ring.radius + ring.radial_thickness * 0.5f;
	const float half_height  = ring.height * 0.5f;

	const uint32_t vtx_base = static_cast<uint32_t>(positions.size());
	const uint32_t idx_base = static_cast<uint32_t>(indices.size());

	for (int segment_idx = 0; segment_idx <= ring.segment_count; ++segment_idx) {
		const float angle = (2.0f * glm::pi<float>()) *
			(static_cast<float>(segment_idx) / static_cast<float>(ring.segment_count));

		const float cos_value = std::cos(angle);
		const float sin_value = std::sin(angle);

		positions.emplace_back(vec3 {outer_radius * cos_value, outer_radius * sin_value, +half_height});
		positions.emplace_back(vec3 {inner_radius * cos_value, inner_radius * sin_value, +half_height});
		positions.emplace_back(vec3 {outer_radius * cos_value, outer_radius * sin_value, -half_height});
		positions.emplace_back(vec3 {inner_radius * cos_value, inner_radius * sin_value, -half_height});

		const float u =
			static_cast<float>(segment_idx) / static_cast<float>(ring.segment_count);
		uvs.emplace_back(vec2{u, 1.0f});
		uvs.emplace_back(vec2{u, 0.66f});
		uvs.emplace_back(vec2{u, 0.0f});
		uvs.emplace_back(vec2{u, 0.33f});
	}

	for (int segment_idx = 0; segment_idx < ring.segment_count; ++segment_idx) {
		const uint32_t base = vtx_base + static_cast<uint32_t>(segment_idx * 4);
		const uint32_t next = base + 4;

		indices.emplace_back(base + 0); indices.emplace_back(base + 1); indices.emplace_back(next + 0);
		indices.emplace_back(base + 1); indices.emplace_back(next + 1); indices.emplace_back(next + 0);

		indices.emplace_back(next + 2); indices.emplace_back(base + 3); indices.emplace_back(base + 2);
		indices.emplace_back(next + 2); indices.emplace_back(next + 3); indices.emplace_back(base + 3);

		indices.emplace_back(base + 0); indices.emplace_back(base + 2); indices.emplace_back(next + 0);
		indices.emplace_back(next + 0); indices.emplace_back(base + 2); indices.emplace_back(next + 2);

		indices.emplace_back(next + 1); indices.emplace_back(base + 3); indices.emplace_back(base + 1);
		indices.emplace_back(next + 1); indices.emplace_back(next + 3); indices.emplace_back(base + 3);
	}

	return {
		.vtx_base  = vtx_base,
		.vtx_count = static_cast<uint32_t>((ring.segment_count + 1) * 4),
		.idx_first = idx_base,
		.idx_count = static_cast<uint32_t>(ring.segment_count * 24),
	};
}


Geoslice Meshgen::cone(const Cone& cone)
{
	const uint32_t vtx_base = static_cast<uint32_t>(positions.size());
	const uint32_t idx_base = static_cast<uint32_t>(indices.size());

	const uint32_t apex_idx   = vtx_base + static_cast<uint32_t>(cone.segment_count);
	const uint32_t center_idx = apex_idx + 1;

	for (int i = 0; i < cone.segment_count; ++i) {
		const float angle = (2.0f * glm::pi<float>()) *
			(static_cast<float>(i) / static_cast<float>(cone.segment_count));

		const float cosv = std::cos(angle);
		const float sinv = std::sin(angle);

		positions.emplace_back(vec3 {cone.base_radius * cosv, cone.base_radius * sinv, cone.base_z});

		const float u =
			static_cast<float>(i) / static_cast<float>(cone.segment_count);
		uvs.emplace_back(vec2 {u, 1.0f});
	}

	positions.emplace_back(vec3 {0.0f, 0.0f, cone.apex_z});
	positions.emplace_back(vec3 {0.0f, 0.0f, cone.base_z});

	uvs.emplace_back(vec2 {0.5f, 0.0f});
	uvs.emplace_back(vec2 {0.5f, 1.0f});

	for (int i = 0; i < cone.segment_count; ++i) {
		const uint32_t curr = vtx_base + static_cast<uint32_t>(i);
		const uint32_t next = vtx_base + static_cast<uint32_t>((i + 1) % cone.segment_count);

		indices.emplace_back(curr);
		indices.emplace_back(apex_idx);
		indices.emplace_back(next);
	}

	for (int i = 0; i < cone.segment_count; ++i) {
		const uint32_t curr = vtx_base + static_cast<uint32_t>(i);
		const uint32_t next = vtx_base + static_cast<uint32_t>((i + 1) % cone.segment_count);

		indices.emplace_back(center_idx);
		indices.emplace_back(next);
		indices.emplace_back(curr);
	}

	return {
		.vtx_base = vtx_base,
		.vtx_count = static_cast<uint32_t>(cone.segment_count + 2),
		.idx_first = idx_base,
		.idx_count = static_cast<uint32_t>(cone.segment_count * 6),
	};
}


Geoslice Meshgen::arrow(const Arrow& arrow)
{
	const uint32_t vtx_base = static_cast<uint32_t>(positions.size());
	const uint32_t idx_base = static_cast<uint32_t>(indices.size());

	box({
		{-arrow.shaft_radius, -arrow.shaft_radius, 0.0f},
		{ arrow.shaft_radius,  arrow.shaft_radius, arrow.shaft_length}
	});

	cone({
		arrow.cone_segments,
		arrow.tip_radius,
		arrow.shaft_length,
		arrow.shaft_length + arrow.tip_length
	});

	return {
		.vtx_base  = vtx_base,
		.vtx_count = static_cast<uint32_t>(positions.size()) - vtx_base,
		.idx_first = idx_base,
		.idx_count = static_cast<uint32_t>(indices.size()) - idx_base,
	};
}


Geoslice Meshgen::diamond(const Diamond& diamond)
{
	const float width  = diamond.width  * 0.5f;
	const float height = diamond.height * 0.5f;
	const float depth  = diamond.depth  * 0.5f;
	const vec3  offset = diamond.center_offset;

	const uint32_t vtx_base = static_cast<uint32_t>(positions.size());
	const uint32_t idx_base = static_cast<uint32_t>(indices.size());

	positions.emplace_back(vec3{ 0.0f  + offset.x,  height + offset.y,  0.0f  + offset.z});
	positions.emplace_back(vec3{ 0.0f  + offset.x, -height + offset.y,  0.0f  + offset.z});
	positions.emplace_back(vec3{-width + offset.x,  0.0f   + offset.y, -depth + offset.z});
	positions.emplace_back(vec3{ width + offset.x,  0.0f   + offset.y, -depth + offset.z});
	positions.emplace_back(vec3{ width + offset.x,  0.0f   + offset.y,  depth + offset.z});
	positions.emplace_back(vec3{-width + offset.x,  0.0f   + offset.y,  depth + offset.z});

	const uint32_t local_indices[24] = {
		0, 3, 2,  0, 4, 3,  0, 5, 4,  0, 2, 5,
		1, 2, 3,  1, 3, 4,  1, 4, 5,  1, 5, 2
	};

	for (uint32_t i = 0; i < 24; ++i) {
		indices.emplace_back(vtx_base + local_indices[i]);
	}

	uvs.emplace_back(vec2 {0.5f,  1.0f});
	uvs.emplace_back(vec2 {0.5f,  0.0f});
	uvs.emplace_back(vec2 {0.0f,  0.5f});
	uvs.emplace_back(vec2 {0.25f, 0.5f});
	uvs.emplace_back(vec2 {0.5f,  0.5f});
	uvs.emplace_back(vec2 {0.75f, 0.5f});

	return {
		.vtx_base  = vtx_base,
		.vtx_count = 6,
		.idx_first = idx_base,
		.idx_count = 24,
	};
}

} // hpr::geo

