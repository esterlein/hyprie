#include "hprint.hpp"

#include "surface.hpp"

#include "sokol_app.h"
#include "sokol_glue.h"


namespace hpr::rdr {


SurfaceInfo query_surface_info()
{
	SurfaceInfo surface_info {};

	surface_info.width  = static_cast<uint32_t>(sapp_width());
	surface_info.height = static_cast<uint32_t>(sapp_height());

	if (surface_info.width  <= 0)
		surface_info.width   = 1;
	if (surface_info.height <= 0)
		surface_info.height  = 1;

	surface_info.aspect =
		static_cast<float>(surface_info.width) /
		static_cast<float>(surface_info.height);

	surface_info.dpi = sapp_dpi_scale();

	surface_info.sample_count = sglue_swapchain().sample_count;
	surface_info.color_format = sglue_swapchain().color_format;
	surface_info.depth_format = sglue_swapchain().depth_format;

	return surface_info;
}


} // hpr::rdr

