#include "nuklear_cfg.hpp"

#include "ui_alloc.hpp"
#include "ui_context.hpp"


namespace hpr::ui {


void UiContext::init()
{
	nk_allocator ui_alloc = MetapoolNuklearAllocator<mtp::default_set>::make();
	nk_init(&m_ctx, &ui_alloc, nullptr);
}

void UiContext::shutdown()
{
	m_panels.clear();
	m_next_panel_idx  = 1;
	m_wants_mouse    = false;
	m_wants_keyboard = false;
	m_fb_width       = 1;
	m_fb_height      = 1;
	m_dpi            = 1.0f;
}

void UiContext::on_resize(int fb_width, int fb_height, float dpi_scale)
{
	m_fb_width  = fb_width;
	m_fb_height = fb_height;
	m_dpi       = dpi_scale;
}

void UiContext::sync_input_screen(const hpr::io::InputState& input_state)
{
	nk_input_begin(&m_ctx);

	nk_input_motion(&m_ctx, static_cast<int>(input_state.mouse_x), static_cast<int>(input_state.mouse_y));
	nk_input_button(&m_ctx, NK_BUTTON_LEFT,   static_cast<int>(input_state.mouse_x), static_cast<int>(input_state.mouse_y), input_state.mouse_left);
	nk_input_button(&m_ctx, NK_BUTTON_RIGHT,  static_cast<int>(input_state.mouse_x), static_cast<int>(input_state.mouse_y), input_state.mouse_right);
	nk_input_button(&m_ctx, NK_BUTTON_MIDDLE, static_cast<int>(input_state.mouse_x), static_cast<int>(input_state.mouse_y), input_state.mouse_middle);

	if (input_state.scroll_x != 0.0f || input_state.scroll_y != 0.0f)
		nk_input_scroll(&m_ctx, nk_vec2(input_state.scroll_x, input_state.scroll_y));

	for (uint32_t cp : input_state.characters)
		nk_input_unicode(&m_ctx, cp);

	nk_input_key(&m_ctx, NK_KEY_SHIFT,        input_state.key_shift);
	nk_input_key(&m_ctx, NK_KEY_CTRL,         input_state.key_ctrl);
	nk_input_key(&m_ctx, NK_KEY_DEL,          input_state.key_delete);
	nk_input_key(&m_ctx, NK_KEY_ENTER,        input_state.key_enter);
	nk_input_key(&m_ctx, NK_KEY_TAB,          input_state.key_tab);
	nk_input_key(&m_ctx, NK_KEY_BACKSPACE,    input_state.key_backspace);
	nk_input_key(&m_ctx, NK_KEY_LEFT,         input_state.key_left);
	nk_input_key(&m_ctx, NK_KEY_RIGHT,        input_state.key_right);
	nk_input_key(&m_ctx, NK_KEY_UP,           input_state.key_up);
	nk_input_key(&m_ctx, NK_KEY_DOWN,         input_state.key_down);
	nk_input_key(&m_ctx, NK_KEY_TEXT_START,   input_state.key_home);
	nk_input_key(&m_ctx, NK_KEY_TEXT_END,     input_state.key_end);
	nk_input_key(&m_ctx, NK_KEY_SCROLL_START, input_state.key_home);
	nk_input_key(&m_ctx, NK_KEY_SCROLL_END,   input_state.key_end);

	nk_input_end(&m_ctx);

	m_wants_mouse = nk_window_is_any_hovered(&m_ctx) || m_ctx.input.mouse.grabbed;
	m_wants_keyboard = m_ctx.input.keyboard.text_len > 0;
}

void UiContext::frame(float delta_time)
{
	(void)delta_time;
	for (auto& panel : m_panels) {
		if (panel.visible && panel.func)
			panel.func(&m_ctx, panel.state);
	}
}

uint32_t UiContext::add_panel(PanelFunc func, void* state, bool visible)
{
	const uint32_t panel_id = m_next_panel_idx++;
	m_panels.push_back(PanelEntry{panel_id, func, state, visible});
	return panel_id;
}

void UiContext::set_panel_visible(uint32_t panel_id, bool visible)
{
	for (auto& panel : m_panels) {
		if (panel.id == panel_id) {
			panel.visible = visible;
			break;
		}
	}
}

nk_context* UiContext::backend_context()
{
	return &m_ctx;
}

bool UiContext::wants_mouse() const
{
	return m_wants_mouse;
}

bool UiContext::wants_keyboard() const
{
	return m_wants_keyboard;
}

} // hpr::ui

