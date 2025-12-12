#include "ui_backend.hpp"
#include "ui_alloc.hpp"


namespace hpr::ui {


UiBackend::UiBackend(rdr::RenderForge& render_forge, FontTextureResolver& font_resolver)
	: m_render_forge         {&render_forge}
	, m_font_resolver        {&font_resolver}
	, m_atlas                {}
	, m_atlas_init           {false}
	, m_default_font         {nullptr}
	, m_null_texture         {}
	, m_default_font_texture {}
{}

void UiBackend::rebuild_default_font(float dpi_scale)
{
	nk_allocator ui_alloc = MetapoolNuklearAllocator<mtp::default_set>::make();

	if (!m_atlas_init) {
		nk_font_atlas_init(&m_atlas, &ui_alloc);
		m_atlas_init = true;
	}
	else {
		nk_font_atlas_clear(&m_atlas);
		nk_font_atlas_init(&m_atlas, &ui_alloc);
	}

	nk_font_atlas_begin(&m_atlas);

	m_default_font = nk_font_atlas_add_default(&m_atlas, 13.0f * dpi_scale, nullptr);

	int atlas_width  = 0;
	int atlas_height = 0;

	const void* pixels_rgba32 = nk_font_atlas_bake(
		&m_atlas,
		&atlas_width,
		&atlas_height,
		NK_FONT_ATLAS_RGBA32
	);

	if (m_render_forge) {
		m_default_font_texture = m_render_forge->create_font_texture(
			pixels_rgba32,
			atlas_width,
			atlas_height
		);
	}
	else {
		m_default_font_texture = {};
	}

	nk_handle tex = nk_handle_id(0);

	if (m_font_resolver) {
		const rdr::FontTexture* font_texture_data =
			m_font_resolver->resolve(m_default_font_texture);
		if (font_texture_data) {
			tex = nk_handle_id(static_cast<int>(font_texture_data->image.id));
		}
	}

	nk_font_atlas_end(&m_atlas, tex, &m_null_texture);
}

nk_font* UiBackend::default_font()
{
	return m_default_font;
}

const nk_draw_null_texture& UiBackend::null_texture() const
{
	return m_null_texture;
}

FontTextureHandle UiBackend::default_font_texture() const
{
	return m_default_font_texture;
}

void UiBackend::shutdown()
{
	if (m_atlas_init) {
		nk_font_atlas_clear(&m_atlas);
		m_atlas_init = false;
	}
	m_default_font = nullptr;
	m_null_texture = {};
	m_default_font_texture = {};
}

} // hpr::ui

