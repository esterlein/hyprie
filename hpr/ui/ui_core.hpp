#pragma once

#include "hprint.hpp"

#include <array>

#include "panic.hpp"
#include "font_data.hpp"


namespace hpr::ui {


struct Rect
{
	int x;
	int y;
	int w;
	int h;
};


struct Canvas
{
	Rect              rect;
	Handle<rdr::Font> font;
};


struct HudStyle
{
	int margin_cols;
	int margin_rows;

	int top_rows_div;
	int log_cols_div;

	int top_log_del;
};


struct DebugLayout
{
	Canvas canvas;

	Rect top;
	Rect log;
	Rect telemetry;

	int cols;
	int rows;

	int log_rows_max;
	int log_cols_max;

	int cell_w;
	int cell_h;
};


namespace cfg {


inline constexpr HudStyle debug_style {
	.margin_cols = 3,
	.margin_rows = 0,

	.top_rows_div = 8,
	.log_cols_div = 3
};

} // hpr::ui::cfg


inline Canvas fit_aspect(
	int                        framebuffer_w,
	int                        framebuffer_h,
	const rdr::HudFontPresets& hud_font_presets
)
{
	rdr::HudFontPresets::Preset preset = hud_font_presets.slots.back();
	for (const auto& font_preset : hud_font_presets.slots) {
		if (font_preset.hud_w <= framebuffer_w && font_preset.hud_h <= framebuffer_h) {
			preset = font_preset;
			break;
		}
	}

	return Canvas {
		.rect = {
			.x = (framebuffer_w - preset.hud_w) / 2,
			.y = (framebuffer_h - preset.hud_h) / 2,
			.w = preset.hud_w,
			.h = preset.hud_h
		},
		.font = preset.font
	};
}


inline DebugLayout build_debug_layout(
	const Canvas&   canvas,
	int             cell_w,
	int             cell_h,
	const HudStyle& style
)
{
	HPR_ASSERT_MSG(canvas.rect.w > 0 && canvas.rect.h > 0,
		"canvas size is non-positive");
	HPR_ASSERT_MSG(cell_w > 0 && cell_h > 0,
		"cell size is non-positive");

	HPR_ASSERT_MSG(
		style.margin_cols  >= 0 &&
		style.margin_rows  >= 0 &&
		style.top_rows_div >  0 &&
		style.log_cols_div >  0,
			"hud style contains invalid values"
	);

	const int margin_w = style.margin_cols * cell_w;
	const int margin_h = style.margin_rows * cell_h;

	HPR_ASSERT_MSG(margin_w * 2 < canvas.rect.w && margin_h * 2 < canvas.rect.h,
		"margins exceed canvas");

	const int raw_inner_w = canvas.rect.w - 2 * margin_w;
	const int raw_inner_h = canvas.rect.h - 2 * margin_h;

	int inner_w = (raw_inner_w / cell_w) * cell_w;
	int inner_h = (raw_inner_h / cell_h) * cell_h;

	const int extra_x = (raw_inner_w - inner_w) / 2;
	const int extra_y = (raw_inner_h - inner_h) / 2;

	DebugLayout layout {};
	layout.canvas = canvas;

	layout.cols = inner_w / cell_w;
	layout.rows = inner_h / cell_h;

	const Rect inner {
		canvas.rect.x + margin_w + extra_x,
		canvas.rect.y + margin_h + extra_y,
		inner_w,
		inner_h
	};

	const int top_rows = layout.rows / style.top_rows_div;
	HPR_ASSERT_MSG(top_rows > 0,
		"number of top rows is non-positive");

	const int top_h = top_rows * cell_h;

	layout.top = {
		inner.x,
		inner.y,
		inner.w,
		top_h
	};

	const Rect body {
		inner.x,
		inner.y + top_h,
		inner.w,
		inner.h - top_h
	};

	layout.log_rows_max  = layout.rows - top_rows;
	HPR_ASSERT_MSG(layout.log_rows_max > 0,
		"number of log rows is non-positive");

	layout.log_cols_max  = layout.cols / style.log_cols_div;
	HPR_ASSERT_MSG(layout.log_cols_max > 0,
		"number of log cols is non-positive");

	const int logs_w = layout.log_cols_max * cell_w;

	layout.log = {
		body.x,
		body.y,
		logs_w,
		body.h
	};

	layout.telemetry = {
		body.x + logs_w,
		body.y,
		body.w - logs_w,
		body.h
	};

	layout.cell_w = cell_w;
	layout.cell_h = cell_h;

	return layout;
}

} // hpr::ui
