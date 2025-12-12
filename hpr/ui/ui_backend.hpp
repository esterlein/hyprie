#pragma once

#include "nuklear_cfg.hpp"
#include "render_forge.hpp"
#include "handle_resolver.hpp"
#include "render_data.hpp"


namespace hpr::ui {


using FontTextureHandle = Handle<rdr::FontTexture>;

using FontTextureResolver = res::HandleResolver<
	res::ResolverEntry<rdr::FontTexture, const res::HandleStore<rdr::FontTexture>>
>;

class UiBackend
{
public:

	UiBackend(rdr::RenderForge& render_forge, FontTextureResolver& font_resolver);

	void rebuild_default_font(float dpi_scale);

	nk_font* default_font();
	const nk_draw_null_texture& null_texture() const;
	FontTextureHandle default_font_texture() const;

	void shutdown();

private:

	rdr::RenderForge*    m_render_forge;
	FontTextureResolver* m_font_resolver;

	nk_font_atlas        m_atlas;
	bool                 m_atlas_init;
	nk_font*             m_default_font;
	nk_draw_null_texture m_null_texture;
	FontTextureHandle    m_default_font_texture;
};

} // hpr::ui

