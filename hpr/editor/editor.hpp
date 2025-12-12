#pragma once

#include "meshgen.hpp"
#include "editor_data.hpp"
#include "render_forge.hpp"


namespace hpr::edt {


inline GizmoPrimitives create_gizmo_primitives(rdr::RenderForge& forge)
{
	GizmoPrimitives gizmo_primitives {};

	mtp::vault<vec3, mtp::default_set>     positions;
	mtp::vault<uint32_t, mtp::default_set> indices;

	gizmo_primitives.program = forge.overlay_program();

	gizmo_primitives.topology.ring_segments = 64;
	gizmo_primitives.topology.cone_segments = 24;

	gizmo_primitives.topology_key =
		(static_cast<uint64_t>(gizmo_primitives.topology.ring_segments) << 32)
		^ static_cast<uint64_t>(gizmo_primitives.topology.cone_segments);

	gizmo_primitives.style.axis_len_px    = 150.0f;
	gizmo_primitives.style.axis_thick_px  = 8.0f;
	gizmo_primitives.style.cone_len_px    = 20.0f;
	gizmo_primitives.style.cone_rad_px    = 10.0f;
	gizmo_primitives.style.plane_side_px  = 70.0f;
	gizmo_primitives.style.ring_radius_px = 70.0f;
	gizmo_primitives.style.ring_thick_px  = 10.0f;
	gizmo_primitives.style.ring_height_px = 10.0f;
	gizmo_primitives.style.tip_cube_px    = 20.0f;
	gizmo_primitives.style.alpha_plane    = 0.35f;
	gizmo_primitives.style.alpha_ring     = 1.00f;
	gizmo_primitives.style.alpha_axis     = 1.00f;

	GeometryRange range_arrow {};
	GeometryRange range_ring  {};
	GeometryRange range_quad  {};
	GeometryRange range_cube  {};

	build_gizmo_geometry(
		positions,
		indices,
		range_arrow,
		range_ring,
		range_quad,
		range_cube,
		gizmo_primitives.style,
		gizmo_primitives.topology.ring_segments,
		gizmo_primitives.topology.cone_segments
	);

	gizmo_primitives.submesh_arrow = 0;
	gizmo_primitives.submesh_ring  = 1;
	gizmo_primitives.submesh_quad  = 2;
	gizmo_primitives.submesh_cube  = 3;

	gizmo_primitives.mesh = forge.create_overlay_mesh(positions, indices, {range_arrow, range_ring, range_quad, range_cube}, gizmo_primitives.topology_key);

	return gizmo_primitives;
}

} // hpr::edt

