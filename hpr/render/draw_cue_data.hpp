#pragma once

#include "hprint.hpp"

#include "math.hpp"


namespace hpr::rdr {


struct CueDescriptor
{
	uint32_t palette_slice {0};
	uint32_t tilemap_slice {0};

	mat4 mtx_M {1.0f};

};

} // hpr::rdr
