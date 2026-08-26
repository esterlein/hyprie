#pragma once

#include "hprint.hpp"
#include "handle.hpp"
#include "mtp_memory.hpp"

#include "texture_data.hpp"
#include "pixel_format.hpp"
#include "render_context.hpp"

#include "sokol_gfx.h"


namespace hpr::rdr {


class EnvironmentMass
{
public:

	EnvironmentMass() = default;

public:

	Environment stage(
		const void* pixel_data,
		uint32_t    width,
		uint32_t    height
	)
	{
		HPR_ASSERT(pixel_data);
		HPR_ASSERT(width  > 0);
		HPR_ASSERT(height > 0);

		Environment environment {};

		sg_image_desc env_desc {};
		env_desc.type                   = SG_IMAGETYPE_CUBE;
		env_desc.width                  = 512;
		env_desc.height                 = 512;
		env_desc.pixel_format           = SG_PIXELFORMAT_RGBA16F;
		env_desc.usage.color_attachment = true;
		env_desc.num_mipmaps            = 1;

		environment.env_cube = sg_make_image(&env_desc);

		sg_image_desc irr_desc {};
		irr_desc.type                   = SG_IMAGETYPE_CUBE;
		irr_desc.width                  = 32;
		irr_desc.height                 = 32;
		irr_desc.pixel_format           = SG_PIXELFORMAT_RGBA16F;
		irr_desc.usage.color_attachment = true;
		irr_desc.num_mipmaps            = 1;

		environment.irr_cube = sg_make_image(&irr_desc);

		sg_image_desc pref_desc {};
		pref_desc.type                   = SG_IMAGETYPE_CUBE;
		pref_desc.width                  = 128;
		pref_desc.height                 = 128;
		pref_desc.pixel_format           = SG_PIXELFORMAT_RGBA16F;
		pref_desc.usage.color_attachment = true;
		pref_desc.num_mipmaps            = 5;

		environment.pref_cube = sg_make_image(&pref_desc);

		sg_image_desc lut_desc {};
		lut_desc.type                   = SG_IMAGETYPE_2D;
		lut_desc.width                  = 512;
		lut_desc.height                 = 512;
		lut_desc.pixel_format           = SG_PIXELFORMAT_RG16F;
		lut_desc.usage.color_attachment = true;
		lut_desc.num_mipmaps            = 1;

		environment.brdf_lut = sg_make_image(&lut_desc);

		sg_view_desc env_view_desc {};
		env_view_desc.texture.image = environment.env_cube;
		environment.env_view = sg_make_view(&env_view_desc);

		sg_view_desc irr_view_desc {};
		irr_view_desc.texture.image = environment.irr_cube;
		environment.irr_view = sg_make_view(&irr_view_desc);

		sg_view_desc pref_view_desc {};
		pref_view_desc.texture.image = environment.pref_cube;
		environment.pref_view = sg_make_view(&pref_view_desc);

		sg_view_desc lut_view_desc {};
		lut_view_desc.texture.image = environment.brdf_lut;
		environment.brdf_view = sg_make_view(&lut_view_desc);

		m_active_environment = environment;

		sg_image_desc src_desc {};
		src_desc.width  = static_cast<int>(width);
		src_desc.height = static_cast<int>(height);
		src_desc.pixel_format = SG_PIXELFORMAT_RGBA32F;
		src_desc.data.mip_levels[0].ptr = pixel_data;
		src_desc.data.mip_levels[0].size =
			static_cast<size_t>(width)  *
			static_cast<size_t>(height) *
			4 * sizeof(float);

		m_equirect_src = sg_make_image(&src_desc);

		return environment;
	}


	sg_image equirect_source() const
	{
		return m_equirect_src;
	}


	void free_equirect_source()
	{
		if (m_equirect_src.id != SG_INVALID_ID) {
			sg_destroy_image(m_equirect_src);
			m_equirect_src.id = SG_INVALID_ID;
		}
	}

	EnvironmentBind bind_state() const
	{
		EnvironmentBind bind_state {};

		bind_state.equirect_src_img = m_equirect_src;
		bind_state.env_cube_img     = m_active_environment.env_cube;
		bind_state.irr_cube_img     = m_active_environment.irr_cube;
		bind_state.pref_cube_img    = m_active_environment.pref_cube;
		bind_state.brdf_lut_img     = m_active_environment.brdf_lut;

		bind_state.env_view  = m_active_environment.env_view;
		bind_state.irr_view  = m_active_environment.irr_view;
		bind_state.pref_view = m_active_environment.pref_view;
		bind_state.brdf_view = m_active_environment.brdf_view;

		return bind_state;
	}

private:

	Environment m_active_environment {};
	sg_image    m_equirect_src       {SG_INVALID_ID};
};


} // hpr::rdr
