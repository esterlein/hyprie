#pragma once

#include "math.hpp"

#include "meshgen.hpp"
#include "render_hub.hpp"
#include "render_forge.hpp"
#include "canonical_data.hpp"
#include "vertex_format.hpp"


namespace hpr::geo {


inline CanonicalShapes create_canonical_shapes(
	rdr::RenderForge&     forge,
	const rdr::RenderHub& hub
)
{
	CanonicalShapes prims {};

	{
		geo::Meshgen meshgen_solid;

		prims.geo_slice[static_cast<size_t>(CanonicalSubmesh::Quad)] = meshgen_solid.quad(
			geo::Quad {.half_extent = 0.5f}
		);
		prims.geo_slice[static_cast<size_t>(CanonicalSubmesh::Box)] = meshgen_solid.box(
			geo::Box {.min = vec3(-0.5f), .max = vec3(0.5f)}
		);
		prims.geo_slice[static_cast<size_t>(CanonicalSubmesh::Diamond)] = meshgen_solid.diamond(
			geo::Diamond {.width = 1.0f, .height = 1.0f, .depth = 1.0f}
		);

		std::array<geo::Geoslice, static_cast<size_t>(CanonicalSubmesh::solid_count)> slices_solid = {
			prims.geo_slice[static_cast<size_t>(CanonicalSubmesh::Quad)],
			prims.geo_slice[static_cast<size_t>(CanonicalSubmesh::Box)],
			prims.geo_slice[static_cast<size_t>(CanonicalSubmesh::Diamond)]
		};

		auto hnd_solid = forge.create_procedural_mesh <
			rdr::RenderForge::MassDomain::assembler, rdr::GenericVertex>(
				meshgen_solid.positions,
				meshgen_solid.indices,
				meshgen_solid.uvs,
				slices_solid
			);

		auto ptr_solid = hub.get(hnd_solid);

		prims.vtx_base_solid  = ptr_solid->vtx_base;
		prims.idx_first_solid = ptr_solid->idx_first;
	}

	{
		geo::Meshgen meshgen_wire;

		prims.geo_slice[static_cast<size_t>(CanonicalSubmesh::BoxWire)] = meshgen_wire.box_wire(
			geo::Box {.min = vec3(-0.5f), .max = vec3(0.5f)}
		);

		std::array<geo::Geoslice, static_cast<size_t>(CanonicalSubmesh::wire_count)> slices_wire = {
			prims.geo_slice[static_cast<size_t>(CanonicalSubmesh::BoxWire)]
		};

		auto hnd_wire = forge.create_procedural_mesh <
			rdr::RenderForge::MassDomain::storage, rdr::GenericVertex>(
				meshgen_wire.positions,
				meshgen_wire.indices,
				meshgen_wire.uvs,
				slices_wire
			);

		auto ptr_wire = hub.get(hnd_wire);

		prims.vtx_base_wire  = ptr_wire->vtx_base;
		prims.idx_first_wire = ptr_wire->idx_first;
	}

	return prims;
}

} // hpr::geo
