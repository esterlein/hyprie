#pragma once

#include "hprint.hpp"
#include "panic.hpp"
#include "log.hpp"

#include "pixel_format.hpp"
#include "texture_format.hpp"
#include "render_context.hpp"

#include "sokol_gfx.h"


namespace hpr::rdr {


class PaletteMass
{
public:

	PaletteMass() = default;

	~PaletteMass()
	{
		release();
	}

	void init(
		const void* palette_data,
		uint32_t    num_palettes,
		uint32_t    colors_per_palette = 256
	)
	{
		HPR_ASSERT(palette_data);
		HPR_ASSERT(num_palettes > 0);
		HPR_ASSERT(colors_per_palette > 0);

		sg_image_desc img_desc {};
		img_desc.type         = SG_IMAGETYPE_2D;
		img_desc.width        = colors_per_palette;
		img_desc.height       = num_palettes;
		img_desc.pixel_format = to_sokol_pixel_format(PixelFormat::rgba8_unorm);
		img_desc.num_mipmaps  = 1;

		img_desc.data.mip_levels[0].ptr  = palette_data;
		img_desc.data.mip_levels[0].size =
			static_cast<size_t>(num_palettes)       *
			static_cast<size_t>(colors_per_palette) *
			bytes_per_pixel_format(PixelFormat::rgba8_unorm);

		m_image = sg_make_image(&img_desc);

		sg_view_desc view_desc {};
		view_desc.texture.image = m_image;
		m_view = sg_make_view(&view_desc);
	}

	void release()
	{
		if (m_image.id) {
			sg_destroy_image(m_image);
			m_image = {};
		}
		if (m_view.id) {
			sg_destroy_view(m_view);
			m_view = {};
		}
	}

	AtlasBind bind_state() const
	{
		AtlasBind state {};

		state.view = m_view;

		return state;
	}

private:

	sg_image   m_image   {};
	sg_view    m_view    {};
};

} // hpr::rdr
