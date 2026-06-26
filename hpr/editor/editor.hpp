#pragma once

#include "shapes.hpp"
#include "meshgen.hpp"
#include "render_hub.hpp"
#include "editor_data.hpp"
#include "render_forge.hpp"
#include "vertex_format.hpp"


namespace hpr::edt {


inline GizmoPrimitives create_gizmo_primitives(
	rdr::RenderForge&     forge,
	const rdr::RenderHub& hub
)
{
	GizmoPrimitives prims {};
	geo::Meshgen meshgen;

	prims.topology.ring_segments = 64;
	prims.topology.cone_segments = 24;

	auto& style = prims.style;

	style.axis_len_px    = 150.0f;
	style.axis_thick_px  = 8.0f;
	style.cone_len_px    = 20.0f;
	style.cone_rad_px    = 10.0f;
	style.ring_radius_px = 70.0f;
	style.ring_thick_px  = 10.0f;
	style.ring_height_px = 10.0f;
	style.plane_side_px  = 80.0f;
	style.tip_cube_px    = 15.0f;
	style.alpha_plane    = 1.0f;
	style.alpha_ring     = 1.0f;
	style.alpha_axis     = 1.0f;

	const float shaft_len = glm::max(0.0f, 1.0f - style.cone_len_px / style.axis_len_px);
	const float tip_len   = glm::min(1.0f,        style.cone_len_px / style.axis_len_px);

	const float shaft_rad = style.axis_thick_px / style.axis_len_px;
	const float tip_rad   = style.cone_rad_px   / style.axis_len_px;

	const float ring_thick  = style.ring_thick_px  / style.ring_radius_px;
	const float ring_height = style.ring_height_px / style.ring_radius_px;

	prims.geo_range[static_cast<size_t>(GizmoSubmesh::Arrow)] = meshgen.arrow(geo::Arrow {
		.cone_segments = prims.topology.cone_segments,
		.shaft_length  = shaft_len,
		.shaft_radius  = shaft_rad,
		.tip_length    = tip_len,
		.tip_radius    = tip_rad
	});

	prims.geo_range[static_cast<size_t>(GizmoSubmesh::Ring)] = meshgen.ring_solid(geo::RingSolid {
		.segment_count    = prims.topology.ring_segments,
		.radius           = 1.0f,
		.radial_thickness = ring_thick,
		.height           = ring_height
	});

	prims.geo_range[static_cast<size_t>(GizmoSubmesh::Quad)] = meshgen.quad(geo::Quad {
		.half_extent = 0.5f
	});

	prims.geo_range[static_cast<size_t>(GizmoSubmesh::Cube)] = meshgen.box(geo::Box {
		.min = vec3(-0.5f),
		.max = vec3( 0.5f)
	});

	auto mesh_hnd = forge.create_procedural_mesh<rdr::GenericVertex>(
		meshgen.positions,
		meshgen.indices,
		meshgen.uvs,
		prims.geo_range
	);

	auto mesh_ptr = hub.get(mesh_hnd);

	prims.vtx_base  = mesh_ptr->vtx_base;
	prims.idx_first = mesh_ptr->idx_first;

	return prims;
}


} // hpr::edt

