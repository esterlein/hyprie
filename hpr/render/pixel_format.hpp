#pragma once

#include "hprint.hpp"

#include "sokol_gfx.h"
#include <cstdint>


namespace hpr::rdr {


enum class PixelFormat : uint8_t
{
	r8_unorm,
	r16uint,
	rgba8_unorm,
	rgba8_srgb
};


static inline sg_pixel_format to_sokol_pixel_format(PixelFormat format)
{
	switch (format) {
		case PixelFormat::r8_unorm:    return SG_PIXELFORMAT_R8;
		case PixelFormat::r16uint:     return SG_PIXELFORMAT_R16UI;
		case PixelFormat::rgba8_unorm: return SG_PIXELFORMAT_RGBA8;
		case PixelFormat::rgba8_srgb:  return SG_PIXELFORMAT_SRGB8A8;
	}
	return SG_PIXELFORMAT_RGBA8;
}


static inline uint32_t bytes_per_pixel_format(PixelFormat format)
{
	switch (format) {
		case PixelFormat::r8_unorm:    return 1;
		case PixelFormat::r16uint:     return 2;
		case PixelFormat::rgba8_unorm: return 4;
		case PixelFormat::rgba8_srgb:  return 4;
	}
	return 4;
}

} // hpr::rdr
