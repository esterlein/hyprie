#pragma once

#include "hprint.hpp"
#include "mtp_memory.hpp"

#include "math.hpp"
#include "shapes.hpp"
#include "geometry_data.hpp"


namespace hpr::geo {


struct Meshgen
{
	mtp::vault<vec3,     mtp::default_set> positions;
	mtp::vault<uint32_t, mtp::default_set> indices;
	mtp::vault<vec2,     mtp::default_set> uvs;

	Geoslice quad(const Quad& quad);
	Geoslice box(const Box& box);

	Geoslice ring(const Ring& ring);
	Geoslice ring_solid(const RingSolid& ring_solid);

	Geoslice cone(const Cone& cone);
	Geoslice arrow(const Arrow& arrow);

	Geoslice diamond(const Diamond& diamond);
};


} // hpr::geo

