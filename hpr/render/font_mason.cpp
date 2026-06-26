#define STBTT_STATIC
#define STB_TRUETYPE_IMPLEMENTATION
#include "stb_truetype.h"

#include "font_mason.hpp"

#include <cmath>

#include "font_data.hpp"
#include "filesystem.hpp"


namespace hpr::rdr {


void FontMason::install_debug_fonts()
{
	FontSpec font_spec {
		.face = FontFace::courier,
		.px   = cfg::debug_font_size
	};

	constexpr const char* atlas_key = "font/proggyclean/atlas_22";
	constexpr const char* font_uri  = "font://ProggyClean.ttf";

	auto ttf_result = fs::read_bin_file<uint8_t>(font_uri);
	HPR_ASSERT_MSG(ttf_result.has_value(), 
		"failed to load font file [%s]", font_uri);

	auto& ttf_bytes = ttf_result.value();

	stbtt_fontinfo font_info {};
	const int font_offset = stbtt_GetFontOffsetForIndex(ttf_bytes.data(), 0);
	HPR_ASSERT_MSG(font_offset >= 0, 
		"failed to get font offset [%s]", font_uri);

	const int init_ok = stbtt_InitFont(&font_info, ttf_bytes.data(), font_offset);
	HPR_ASSERT_MSG(init_ok != 0, 
		"failed to init truetype font [%s]", font_uri);

	constexpr int first_char = 32;
	constexpr int num_chars  = 95;
	constexpr int atlas_w    = 512;
	constexpr int atlas_h    = 128;

	mtp::vault<unsigned char, mtp::default_set> atlas;
	atlas.resize(static_cast<size_t>(atlas_w * atlas_h), 0);

	stbtt_bakedchar baked_glyphs[num_chars] {};

	const int bake_result = stbtt_BakeFontBitmap(
		ttf_bytes.data(),
		font_offset,
		static_cast<float>(cfg::debug_font_size),
		atlas.data(),
		atlas_w,
		atlas_h,
		first_char,
		num_chars,
		baked_glyphs
	);

	HPR_ASSERT_MSG(bake_result > 0,
		"failed to bake font atlas");

	const float scale = stbtt_ScaleForPixelHeight(
		&font_info,
		static_cast<float>(cfg::debug_font_size)
	);

	int ascent   = 0;
	int descent  = 0;
	int line_gap = 0;

	stbtt_GetFontVMetrics(&font_info, &ascent, &descent, &line_gap);

	const int line_height_px = static_cast<int>(
		std::round((static_cast<float>(ascent - descent + line_gap)) * scale)
	);

	FontMetrics metrics {
		.first_char  = static_cast<uint16_t>(first_char),
		.num_chars   = static_cast<uint16_t>(num_chars),
		.line_height = static_cast<uint16_t>(line_height_px)
	};


	const float pack_scale_x = 65535.0f / static_cast<float>(atlas_w);
	const float pack_scale_y = 65535.0f / static_cast<float>(atlas_h);

	for (int i = 0; i < num_chars; ++i) {
		const stbtt_bakedchar& glyph_src = baked_glyphs[i];
		FontGlyph& glyph_dst = metrics.glyphs[static_cast<size_t>(i)];

		glyph_dst.u_min =
			static_cast<uint16_t>(static_cast<float>(glyph_src.x0) * pack_scale_x + 0.5f);
		glyph_dst.v_min =
			static_cast<uint16_t>(static_cast<float>(glyph_src.y0) * pack_scale_y + 0.5f);
		glyph_dst.u_max =
			static_cast<uint16_t>(static_cast<float>(glyph_src.x1) * pack_scale_x + 0.5f);
		glyph_dst.v_max =
			static_cast<uint16_t>(static_cast<float>(glyph_src.y1) * pack_scale_y + 0.5f);

		glyph_dst.x_min = glyph_src.xoff;
		glyph_dst.y_min = glyph_src.yoff;
		glyph_dst.x_max = glyph_src.xoff + static_cast<float>(glyph_src.x1 - glyph_src.x0);
		glyph_dst.y_max = glyph_src.yoff + static_cast<float>(glyph_src.y1 - glyph_src.y0);
		
		glyph_dst.advance = glyph_src.xadvance;
		
		glyph_dst.x_min_px = static_cast<int16_t>(std::lround(glyph_src.xoff));
		glyph_dst.y_min_px = static_cast<int16_t>(std::lround(glyph_src.yoff));
		glyph_dst.x_max_px = static_cast<int16_t>(
			std::lround(glyph_src.xoff) + static_cast<int>(glyph_src.x1 - glyph_src.x0)
		);
		glyph_dst.y_max_px = static_cast<int16_t>(
			std::lround(glyph_src.yoff) + static_cast<int>(glyph_src.y1 - glyph_src.y0)
		);

		glyph_dst.advance_px = static_cast<int16_t>(std::lround(glyph_src.xadvance));
	}

	const std::span<const uint8_t> atlas_bytes {
		reinterpret_cast<const uint8_t*>(atlas.data()),
		static_cast<size_t>(atlas_w) * static_cast<size_t>(atlas_h)
	};

	const Handle<res::ImageResource> atlas_image =
		m_keeper.add_memory_image(atlas_key, atlas_w, atlas_h, 1, atlas_bytes);

	m_hud_font_presets.slots = {{
		{2560, 1440, m_forge.create_bitmap_font(font_spec, atlas_image, metrics)},
		{1920, 1080, m_forge.create_bitmap_font(font_spec, atlas_image, metrics)},
		{1600,  900, m_forge.create_bitmap_font(font_spec, atlas_image, metrics)},
		{1280,  720, m_forge.create_bitmap_font(font_spec, atlas_image, metrics)},
	}};
}

} // hpr::rdr
