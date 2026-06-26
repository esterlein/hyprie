#pragma once

#include "hprint.hpp"
#include "math.hpp"

#include "sokol_gfx.h"


namespace hpr::rdr {


struct GridPack
{
	vec4  minor_rgba;
	vec4  major_rgba;
	vec2  minor_vis_range_px;
	vec2  major_vis_range_px;
	
	float line_width_px;
	float cell_size;
	float y_plane;
	float major_step_cells;
};


} // hpr::rdr
