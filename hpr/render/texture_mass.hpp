#pragma once

#include "hprint.hpp"
#include "handle.hpp"
#include "mtp_memory.hpp"

#include "texture_data.hpp"
#include "pixel_format.hpp"
#include "texture_format.hpp"
#include "render_context.hpp"

#include "sokol_gfx.h"
#include "stb_image_resize2.h"

#include <bit>
#include <algorithm>
#include <unordered_set>


namespace hpr::rdr {


class TextureMass
{
public:

	TextureMass()
	{
		init_arrays();
	}

public:

	uint32_t get_array_index(uint32_t bucket_size, PixelFormat pix_fmt) const
	{
		uint32_t bit_pos =
			31U - static_cast<uint32_t>(std::countl_zero(bucket_size));

		uint32_t size_idx = cfg::num_tex_arrays - 1 - bit_pos;

		return (size_idx * 2) + (pix_fmt == PixelFormat::rgba8_srgb ? 0 : 1);
	}


	Texture stage(
		const void* pixel_data,
		uint32_t    width,
		uint32_t    height,
		PixelFormat pix_format
	)
	{
		HPR_ASSERT(pixel_data);
		HPR_ASSERT(width  > 0);
		HPR_ASSERT(height > 0);


		uint32_t max_dim     = std::max({width, height, 64U});
		uint32_t bucket_size = std::min(std::bit_ceil(max_dim), 2048U);

		uint32_t array_idx = get_array_index(bucket_size, pix_format);

		HPR_ASSERT(!m_free_slices[array_idx].empty());

		uint32_t slice_idx = m_free_slices[array_idx].back();
		m_free_slices[array_idx].pop_back();

		uint32_t stride =
			bucket_size * bucket_size * bytes_per_pixel_format(pix_format);

		uint8_t* curr_slice =
			m_stage_buffer[array_idx].data() + (slice_idx * stride);
	
		if (width == bucket_size && height == bucket_size) {
			std::memcpy(curr_slice, pixel_data, stride);
		}
		else {
			if (pix_format == PixelFormat::rgba8_srgb) {
				stbir_resize_uint8_srgb(
					static_cast<const unsigned char*>(pixel_data),
					static_cast<int>(width),
					static_cast<int>(height),
					0,
					static_cast<unsigned char*>(curr_slice),
					static_cast<int>(bucket_size),
					static_cast<int>(bucket_size),
					0,
					STBIR_RGBA
				);
			}
			else {
				stbir_resize_uint8_linear(
					static_cast<const unsigned char*>(pixel_data),
					static_cast<int>(width),
					static_cast<int>(height),
					0,
					static_cast<unsigned char*>(curr_slice),
					static_cast<int>(bucket_size),
					static_cast<int>(bucket_size),
					0,
					STBIR_RGBA
				);
			}
		}
	
		m_dirty_arrays.insert(array_idx);
	
		return Texture {
			.array  = array_idx,
			.slice  = slice_idx,
			.width  = bucket_size,
			.height = bucket_size
		};
	}


	void sync()
	{
		for (uint32_t array_idx : m_dirty_arrays) {

			TextureArray& array = m_tex_arrays[array_idx];

			sg_image_data img_data {};

			img_data.mip_levels[0].ptr  = m_stage_buffer[array_idx].data();
			img_data.mip_levels[0].size =
				static_cast<size_t>(array.width)     *
				static_cast<size_t>(array.height)    *
				bytes_per_pixel_format(array.format) *
				array.num_slices;

			sg_update_image(array.image, &img_data);
		}

		m_dirty_arrays.clear();
	}


	TextureBind bind_state() const
	{
		TextureBind state {};
		state.count = cfg::num_tex_arrays;

		for (uint32_t i = 0; i < cfg::num_tex_arrays; ++i) {
			state.views[i] = m_tex_arrays[i].view;
		}

		return state;
	}

private:

	void init_arrays()
	{
		for (uint32_t i = 0; i < cfg::num_tex_arrays; ++i) {
			
			uint32_t size_idx    = i / 2;
			uint32_t pix_fmt_idx = i % 2;

			uint32_t bit_pos = (cfg::num_tex_arrays - 1) - size_idx;
			uint32_t bucket_size = 1U << bit_pos;
			
			PixelFormat pix_fmt = (pix_fmt_idx == 0) ?
				PixelFormat::rgba8_srgb : PixelFormat::rgba8_unorm;

			sg_image_desc img_desc {};
			img_desc.type         = SG_IMAGETYPE_ARRAY;
			img_desc.width        = static_cast<int>(bucket_size);
			img_desc.height       = static_cast<int>(bucket_size);
			img_desc.num_slices   = cfg::max_num_slices;
			img_desc.pixel_format = to_sokol_pixel_format(pix_fmt);
			img_desc.num_mipmaps  = 1;
			img_desc.usage.dynamic_update = true;

			sg_image image = sg_make_image(&img_desc);

			sg_view_desc view_desc {};

			view_desc.texture.image = image;
			view_desc.texture.mip_levels.base  = 0;
			view_desc.texture.mip_levels.count = 0;
			view_desc.texture.slices.base      = 0;
			view_desc.texture.slices.count     = 0;

			sg_view view = sg_make_view(&view_desc);

			auto& tex_array = m_tex_arrays.emplace_back(); 

			tex_array.image      = image;
			tex_array.view       = view;
			tex_array.width      = bucket_size;
			tex_array.height     = bucket_size;
			tex_array.num_slices = cfg::max_num_slices;
			tex_array.format     = pix_fmt;

			uint32_t total_bytes =
				bucket_size *
				bucket_size *
				bytes_per_pixel_format(pix_fmt) *
				cfg::max_num_slices;

			mtp::vault<uint8_t, mtp::default_set> bucket_buffer;
			bucket_buffer.resize(total_bytes);

			m_stage_buffer.push_back(std::move(bucket_buffer));

			mtp::vault<uint32_t, mtp::default_set> freelist;
			freelist.reserve(cfg::max_num_slices);

			for (int32_t slice = static_cast<int32_t>(cfg::max_num_slices) - 1; slice >= 0; --slice) {
				freelist.push_back(static_cast<uint32_t>(slice));
			}

			m_free_slices.push_back(std::move(freelist));
		}
	}

private:

	mtp::vault<TextureArray, mtp::default_set> m_tex_arrays;

	mtp::vault<mtp::vault<uint32_t, mtp::default_set>, mtp::default_set> m_free_slices;
	mtp::vault<mtp::vault<uint8_t,  mtp::default_set>, mtp::default_set> m_stage_buffer;

	std::unordered_set<uint32_t> m_dirty_arrays;
};


} // hpr::rdr
