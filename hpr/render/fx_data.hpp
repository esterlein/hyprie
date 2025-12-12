#pragma once

#include <cstdint>

#include "sokol_gfx.h"


namespace hpr::rdr {


struct GridPack
{
	float     base_spacing;
	float     target_px;
	float     line_width_px;
	int32_t   major_step;
	uint32_t  minor_rgb888;
	uint32_t  major_rgb888;
	float     grid_y;
};

} // hpr::rdr
