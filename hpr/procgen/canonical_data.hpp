#pragma once

#include "hprint.hpp"
#include "mtp_memory.hpp"

#include "math.hpp"
#include "handle.hpp"
#include "render_data.hpp"
#include "geometry_data.hpp"


namespace hpr::geo {


enum class CanonicalSubmesh : uint32_t
{
	Quad = 0,
	Box,
	BoxWire,
	Diamond,

	count,
	solid_count = 3,
	wire_count  = 1
};

struct CanonicalShapes
{
	std::array<geo::Geoslice, static_cast<size_t>(CanonicalSubmesh::count)> geo_slice {};

	uint32_t vtx_base_solid  {0};
	uint32_t idx_first_solid {0};

	uint32_t vtx_base_wire   {0};
	uint32_t idx_first_wire  {0};
};


} // hpr::geo
