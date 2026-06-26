#pragma once

#include "hprint.hpp"
#include "mtp_memory.hpp"

#include "math.hpp"
#include "handle.hpp"

#include "pixel_format.hpp"
#include "texture_format.hpp"

#include "sokol_gfx.h"


namespace hpr::rdr {


namespace cfg {

inline constexpr uint32_t max_num_slices = 96U;
inline constexpr uint32_t num_tex_arrays = 12U;

} // hpr::rdr::cfg


struct Texture
{
	uint32_t array  {0xFFFFFFFF};
	uint32_t slice  {0xFFFFFFFF};
	uint32_t width  {0};
	uint32_t height {0};
};


struct TextureArray
{
	sg_image image {};
	sg_view  view  {};

	uint32_t width      {0};
	uint32_t height     {0};
	uint32_t num_slices {0};

	PixelFormat format {PixelFormat::rgba8_srgb};

	TextureArray() = default;
	TextureArray(const TextureArray&) = delete;
	TextureArray& operator=(const TextureArray&) = delete;

	TextureArray(TextureArray&& other) noexcept
	{
		image      = other.image;
		view       = other.view;
		width      = other.width;
		height     = other.height;
		num_slices = other.num_slices;
		format     = other.format;

		other.image      = {};
		other.view       = {};
		other.width      = 0;
		other.height     = 0;
		other.num_slices = 0;
		other.format     = PixelFormat::rgba8_srgb;
	}

	TextureArray& operator=(TextureArray&& other) noexcept
	{
		if (this != &other) {

			if (image.id) {
				sg_destroy_image(image);
			}

			if (view.id) {
				sg_destroy_view(view);
			}

			image      = other.image;
			view       = other.view;
			width      = other.width;
			height     = other.height;
			num_slices = other.num_slices;
			format     = other.format;


			other.image      = {};
			other.view       = {};
			other.width      = 0;
			other.height     = 0;
			other.num_slices = 0;
			other.format     = PixelFormat::rgba8_srgb;
		}

		return *this;
	}

	~TextureArray()
	{
		if (image.id) {
			sg_destroy_image(image);
		}

		if (view.id) {
			sg_destroy_view(view);
		}
	}
};


struct Atlas
{
	sg_image   image  {};
	sg_view    view   {};
	uint32_t   width  {0};
	uint32_t   height {0};
};


} // hpr::rdr
