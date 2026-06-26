#pragma once

#include "hprint.hpp"
#include "mtp_memory.hpp"

#include "math.hpp"
#include "handle.hpp"
#include "render_data.hpp"
#include "geometry_data.hpp"
#include <cstdint>


namespace hpr::geo {


enum class CanonicalSubmesh : uint32_t
{
	Quad = 0,
	Box,
	Diamond,

	count
};


struct CanonicalPrimitives
{
	std::array<geo::Geoslice, static_cast<size_t>(CanonicalSubmesh::count)> geo_slice {};

	uint32_t vtx_base  {0};
	uint32_t idx_first {0};
};


} // hpr::geo
