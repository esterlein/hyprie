#pragma once

#include "draw_view_data.hpp"


namespace hpr {


struct FrameContext
{
	rdr::DrawView         scene_view  {};
	rdr::DrawViewLightSet scene_light {};
};

} // hpr

