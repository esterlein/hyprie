#pragma once

#include "hprint.hpp"
#include "panic.hpp"
#include "mtp_memory.hpp"

#include "pixel_format.hpp"
#include "texture_data.hpp"
#include "texture_format.hpp"
#include "render_context.hpp"

#include "sokol_gfx.h"


namespace hpr::rdr {


class FontMass
{
public:

	FontMass() = default;

	explicit FontMass(uint32_t capacity)
	{
		m_atlases.reserve(capacity);
	}

	~FontMass()
	{
		for (auto& atlas : m_atlases) {
			if (atlas.image.id) {
				sg_destroy_image(atlas.image);
			}
			if (atlas.view.id) {
				sg_destroy_view(atlas.view);
			}
		}
	}

	uint32_t add_atlas(const void* pixel_data, uint32_t width, uint32_t height)
	{
		HPR_ASSERT(pixel_data);
		HPR_ASSERT(width  > 0);
		HPR_ASSERT(height > 0);

		sg_image_desc img_desc {};
		img_desc.type         = SG_IMAGETYPE_2D;
		img_desc.width        = width;
		img_desc.height       = height;
		img_desc.pixel_format = to_sokol_pixel_format(PixelFormat::r8_unorm);
		img_desc.num_mipmaps  = 1;
		
		img_desc.data.mip_levels[0].ptr  = pixel_data;
		img_desc.data.mip_levels[0].size =
			static_cast<size_t>(width) * static_cast<size_t>(height);

		sg_image image = sg_make_image(&img_desc);

		sg_view_desc view_desc {};
		view_desc.texture.image = image;

		sg_view view = sg_make_view(&view_desc);

		uint32_t index = static_cast<uint32_t>(m_atlases.size());

		Atlas atlas {
			.image  = image,
			.view   = view,
			.width  = width,
			.height = height
		};

		m_atlases.emplace_back(std::move(atlas));

		return index;
	}

	TextureBind bind_state() const
	{
		TextureBind state {};
		state.count = static_cast<uint32_t>(m_atlases.size());

		HPR_ASSERT(state.count <= 16); 

		for (uint32_t i = 0; i < state.count; ++i) {
			state.views[i] = m_atlases[i].view;
		}

		return state;
	}

private:

	mtp::vault<Atlas, mtp::default_set> m_atlases;
};

} // hpr::rdr
