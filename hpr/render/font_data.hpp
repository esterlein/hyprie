#pragma once

#include "hprint.hpp"

#include <array>

#include "handle.hpp"
#include "render_data.hpp"


namespace hpr::rdr {


namespace cfg {

inline constexpr uint16_t font_first_char       {32U};
inline constexpr uint16_t font_num_chars        {95U};
inline constexpr uint16_t debug_font_size       {22U};
inline constexpr uint32_t hud_font_preset_count {4U};

} // hpr::rdr::cfg


enum class FontFace : uint8_t
{
	courier = 0
};


struct FontSpec
{
	FontFace face {FontFace::courier};
	uint16_t px   {cfg::debug_font_size};
};


struct FontGlyph
{
	uint16_t u_min;
	uint16_t v_min;
	uint16_t u_max;
	uint16_t v_max;

	float x_min;
	float y_min;
	float x_max;
	float y_max;

	float advance;

	int16_t x_min_px;
	int16_t y_min_px;
	int16_t x_max_px;
	int16_t y_max_px;

	int16_t advance_px;
};


struct FontMetrics
{
	uint16_t first_char   {cfg::font_first_char};
	uint16_t num_chars    {cfg::font_num_chars};
	uint16_t line_height {0};

	std::array<FontGlyph, cfg::font_num_chars> glyphs {};
};


struct Font
{
	FontSpec    spec    {};
	FontMetrics metrics {};
	uint32_t    atlas_idx;
};


struct HudFontPresets
{
	struct Preset
	{
		int               hud_w;
		int               hud_h;
		Handle<rdr::Font> font;
	};

	std::array<Preset, cfg::hud_font_preset_count> slots;
};

} // hpr::rdr
