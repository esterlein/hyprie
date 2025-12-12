#pragma once

#include <span>
#include <cstdint>

#include "nuklear_cfg.hpp"

#include "hpr/input/input_state.hpp"


namespace hpr::ui {


using PanelFunc = void (*)(nk_context*, void*);

struct PanelEntry
{
	uint32_t  id;
	PanelFunc func;
	void*     state;
	bool      visible;
};


class UiContext
{
public:

	void init();
	void shutdown();

	void on_resize(int fb_width, int fb_height, float dpi_scale);

	void sync_input_screen(const hpr::io::InputState& input_state);

	void frame(float delta_time);

	uint32_t add_panel(PanelFunc func, void* state, bool visible);
	void set_panel_visible(uint32_t panel_id, bool visible);

	nk_context* backend_context();

	bool wants_mouse() const;
	bool wants_keyboard() const;

private:

	nk_context m_ctx {};

	mtp::vault<PanelEntry, mtp::default_set> m_panels;
	uint32_t m_next_panel_idx {1};

	int   m_fb_width  {1};
	int   m_fb_height {1};
	float m_dpi       {1.0f};

	bool m_wants_mouse    {false};
	bool m_wants_keyboard {false};
};

} // hpr::ui

