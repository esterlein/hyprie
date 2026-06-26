#include "nuklear_cfg.hpp"

#include "ui_alloc.hpp"
#include "ui_style.hpp"
#include "render_forge.hpp"


namespace hpr::ui {


void UiStyle::apply_default(nk_context* ctx, nk_font* font)
{
	if (font) {
		nk_style_set_font(ctx, &font->handle);
	}

	apply_black_flat(ctx, 0.8f);
}


void UiStyle::apply_black_flat(nk_context* ctx, float opacity)
{
	if (!ctx)
		return;

	nk_color bg     = nk_rgba_f(0.02f, 0.02f, 0.03f, opacity);
	nk_color pane   = nk_rgba_f(0.05f, 0.05f, 0.06f, opacity);
	nk_color hover  = nk_rgba_f(0.10f, 0.10f, 0.12f, opacity);
	nk_color active = nk_rgba_f(0.16f, 0.16f, 0.18f, opacity);
	nk_color text   = nk_rgba_f(1.0f, 1.0f, 1.0f, 1.0f);

	ctx->style.window.background       = bg;
	ctx->style.window.fixed_background = nk_style_item_color(bg);
	ctx->style.window.border           = 0.0f;
	ctx->style.window.rounding         = 0.0f;
	ctx->style.window.padding          = nk_vec2(12.0f, 12.0f);
	ctx->style.window.spacing          = nk_vec2(10.0f, 10.0f);

	ctx->style.text.color = text;

	ctx->style.tab.background = nk_style_item_color(pane);
	ctx->style.tab.border     = 0.0f;
	ctx->style.tab.rounding   = 0.0f;

	ctx->style.button.normal      = nk_style_item_color(pane);
	ctx->style.button.hover       = nk_style_item_color(hover);
	ctx->style.button.active      = nk_style_item_color(active);
	ctx->style.button.text_normal = text;
	ctx->style.button.text_hover  = text;
	ctx->style.button.text_active = text;
	ctx->style.button.border      = 0.0f;
	ctx->style.button.rounding    = 0.0f;
	ctx->style.button.padding     = nk_vec2(10.0f, 8.0f);

	ctx->style.edit.normal        = nk_style_item_color(pane);
	ctx->style.edit.hover         = nk_style_item_color(hover);
	ctx->style.edit.active        = nk_style_item_color(active);
	ctx->style.edit.border        = 0.0f;
	ctx->style.edit.rounding      = 0.0f;
	ctx->style.edit.cursor_normal = text;
	ctx->style.edit.cursor_hover  = text;

	ctx->style.property.normal   = nk_style_item_color(pane);
	ctx->style.property.hover    = nk_style_item_color(hover);
	ctx->style.property.active   = nk_style_item_color(active);
	ctx->style.property.border   = 0.0f;
	ctx->style.property.rounding = 0.0f;

	ctx->style.option.normal       = nk_style_item_color(pane);
	ctx->style.option.hover        = nk_style_item_color(hover);
	ctx->style.option.active       = nk_style_item_color(active);
	ctx->style.option.border_color = pane;

	ctx->style.slider.bar_normal    = pane;
	ctx->style.slider.bar_hover     = hover;
	ctx->style.slider.bar_active    = active;
	ctx->style.slider.cursor_normal = nk_style_item_color(active);
	ctx->style.slider.cursor_hover  = nk_style_item_color(active);
	ctx->style.slider.cursor_active = nk_style_item_color(active);
	ctx->style.slider.rounding      = 0.0f;

	ctx->style.progress.normal   = nk_style_item_color(pane);
	ctx->style.progress.hover    = nk_style_item_color(hover);
	ctx->style.progress.active   = nk_style_item_color(active);
	ctx->style.progress.border   = 0.0f;
	ctx->style.progress.rounding = 0.0f;

	ctx->style.scrollh.normal = nk_style_item_color(pane);
	ctx->style.scrollh.hover  = nk_style_item_color(hover);
	ctx->style.scrollh.active = nk_style_item_color(active);
	ctx->style.scrollh.border = 0.0f;

	ctx->style.scrollv.normal = nk_style_item_color(pane);
	ctx->style.scrollv.hover  = nk_style_item_color(hover);
	ctx->style.scrollv.active = nk_style_item_color(active);
	ctx->style.scrollv.border = 0.0f;
}


void UiStyle::shutdown()
{}

} // hpr::ui

