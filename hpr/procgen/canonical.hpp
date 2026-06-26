#pragma once

#include "math.hpp"

#include "meshgen.hpp"
#include "render_hub.hpp"
#include "render_forge.hpp"
#include "canonical_data.hpp"
#include "vertex_format.hpp"


namespace hpr::geo {


inline CanonicalPrimitives create_canonical_primitives(
	rdr::RenderForge&     forge,
	const rdr::RenderHub& hub
)
{
	CanonicalPrimitives prims {};

	geo::Meshgen meshgen;

	prims.geo_slice[static_cast<size_t>(CanonicalSubmesh::Quad)] = meshgen.quad(geo::Quad {
		.half_extent = 0.5f
	});

	prims.geo_slice[static_cast<size_t>(CanonicalSubmesh::Box)] = meshgen.box(geo::Box {
		.min = vec3(-0.5f),
		.max = vec3( 0.5f)
	});

	prims.geo_slice[static_cast<size_t>(CanonicalSubmesh::Diamond)] = meshgen.diamond(geo::Diamond {
		.width  = 1.0f,
		.height = 1.0f,
		.depth  = 1.0f
	});

	auto mesh_hnd = forge.create_procedural_mesh<rdr::GenericVertex>(
		meshgen.positions,
		meshgen.indices,
		meshgen.uvs,
		prims.geo_slice
	);

	auto mesh_ptr = hub.get(mesh_hnd);

	prims.vtx_base  = mesh_ptr->vtx_base;
	prims.idx_first = mesh_ptr->idx_first;

	return prims;
}


} // hpr::geo
