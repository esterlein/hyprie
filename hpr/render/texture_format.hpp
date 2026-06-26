#pragma once

#include "hprint.hpp"

#include "pixel_format.hpp"

#include "sokol_gfx.h"


namespace hpr::rdr {


enum class SamplerType : uint8_t
{
	linear_repeat,
	linear_clamp,
	nearest_repeat,
	nearest_clamp,
	count
};


inline sg_sampler_desc to_sokol_sampler_desc(SamplerType type)
{
	sg_sampler_desc desc {};

	switch (type) {
		case SamplerType::linear_repeat:
			desc.min_filter = SG_FILTER_LINEAR;
			desc.mag_filter = SG_FILTER_LINEAR;
			desc.wrap_u     = SG_WRAP_REPEAT;
			desc.wrap_v     = SG_WRAP_REPEAT;
			break;
		case SamplerType::linear_clamp:
			desc.min_filter = SG_FILTER_LINEAR;
			desc.mag_filter = SG_FILTER_LINEAR;
			desc.wrap_u     = SG_WRAP_CLAMP_TO_EDGE;
			desc.wrap_v     = SG_WRAP_CLAMP_TO_EDGE;
			break;
		case SamplerType::nearest_repeat:
			desc.min_filter = SG_FILTER_NEAREST;
			desc.mag_filter = SG_FILTER_NEAREST;
			desc.wrap_u     = SG_WRAP_REPEAT;
			desc.wrap_v     = SG_WRAP_REPEAT;
			break;
		case SamplerType::nearest_clamp:
			desc.min_filter = SG_FILTER_NEAREST;
			desc.mag_filter = SG_FILTER_NEAREST;
			desc.wrap_u     = SG_WRAP_CLAMP_TO_EDGE;
			desc.wrap_v     = SG_WRAP_CLAMP_TO_EDGE;
			break;
	}

	return desc;
}


} // hpr::rdr

