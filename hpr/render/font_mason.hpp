#pragma once

#include "font_data.hpp"
#include "asset_keeper.hpp"
#include "render_forge.hpp"


namespace hpr::rdr {


class FontMason final
{
public:

	FontMason(res::AssetKeeper& keeper, RenderForge& forge)
		: m_keeper {keeper}
		, m_forge  {forge}
	{}

	~FontMason() = default;

	FontMason(const FontMason&) = delete;
	FontMason& operator=(const FontMason&) = delete;
	FontMason(const FontMason&&) = delete;
	FontMason& operator=(FontMason&&) = delete;

	void install_debug_fonts();
	HudFontPresets debug_fonts() const { return m_hud_font_presets; }

private:

	res::AssetKeeper& m_keeper;
	RenderForge&      m_forge;

	HudFontPresets    m_hud_font_presets;
};


} // hpr::rdr
