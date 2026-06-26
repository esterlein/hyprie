#pragma once

#include "hprint.hpp"

#include "sokol_gfx.h"


namespace hpr::rdr {


struct SurfaceInfo
{
	uint32_t width  {1};
	uint32_t height {1};
	float    aspect {1.0f};
	float    dpi    {1.0f};

	int sample_count {1};

	sg_pixel_format color_format;
	sg_pixel_format depth_format;
};


SurfaceInfo query_surface_info();


} // hpr::rdr
