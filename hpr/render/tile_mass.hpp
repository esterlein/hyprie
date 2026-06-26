#pragma once

#include "hprint.hpp"
#include "mtp_memory.hpp"

#include "pixel_format.hpp"
#include "texture_data.hpp"
#include "texture_format.hpp"
#include "render_context.hpp"

#include "sokol_gfx.h"


namespace hpr::rdr {


struct TileMassSlice
{
	uint32_t atlas  {0xFFFFFFFF};
	uint32_t width  {0};
	uint32_t height {0};
};


class TileMass
{
public:

	TileMass() = default;

	TileMass(uint32_t capacity)
	{
		m_atlases.reserve(capacity);
	}

	TileMassSlice stage(
		uint32_t width,
		uint32_t height
	)
	{
		HPR_ASSERT(width  > 0);
		HPR_ASSERT(height > 0);

		sg_image_desc img_desc {};
		img_desc.type                 = SG_IMAGETYPE_2D;
		img_desc.width                = width;
		img_desc.height               = height;
		img_desc.num_mipmaps          = 1;
		img_desc.pixel_format         = to_sokol_pixel_format(PixelFormat::r16uint);
		img_desc.usage.dynamic_update = true;

		uint32_t index = static_cast<uint32_t>(m_atlases.size());

		Atlas atlas;
		atlas.image   = sg_make_image(&img_desc);
		atlas.width   = width;
		atlas.height  = height;

		sg_view_desc view_desc {};
		view_desc.texture.image = atlas.image;
		atlas.view = sg_make_view(&view_desc);

		m_atlases.emplace_back(std::move(atlas));

		return TileMassSlice {
			.atlas  = index,
			.width  = width,
			.height = height
		};
	}

	TextureBind bind_state() const
	{
		TextureBind state {};
		state.count = static_cast<uint32_t>(m_atlases.size());

		for (uint32_t i = 0; i < state.count; ++i) {
			state.views[i] = m_atlases[i].view;
		}

		return state;
	}

private:

	mtp::vault<Atlas, mtp::default_set> m_atlases;
};

} // hpr::rdr
