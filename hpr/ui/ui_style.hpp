#pragma once


#include "nuklear_cfg.hpp"


namespace hpr::ui {


class UiStyle
{
public:

	void apply_default(nk_context* ctx, nk_font* font);

	void apply_black_flat(nk_context* ctx, float opacity);
	void shutdown();
};

} // hpr::ui

