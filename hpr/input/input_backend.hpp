#pragma once

#include <cstdint>

#include "sokol_app.h"

#include "log.hpp"
#include "input_state.hpp"


namespace hpr::io {


static constexpr float k_deadzone_px    = 6.0f;
static constexpr uint64_t k_hold_frames = 8;


inline void handle_event(const sapp_event* event, InputState& state)
{
	switch (event->type) {

	case SAPP_EVENTTYPE_MOUSE_DOWN:

		if (event->mouse_button == SAPP_MOUSEBUTTON_LEFT) {
			state.mouse_left = true;
			state.mouse_left_press = true;
			state.mouse_press_x = event->mouse_x;
			state.mouse_press_y = event->mouse_y;
		}
		if (event->mouse_button == SAPP_MOUSEBUTTON_RIGHT) {
			state.mouse_right = true;
			state.mouse_right_press = true;
		}
		if (event->mouse_button == SAPP_MOUSEBUTTON_MIDDLE) {
			state.mouse_middle = true;
			state.mouse_middle_press = true;
		}

		if (event->mouse_button == SAPP_MOUSEBUTTON_RIGHT && !sapp_mouse_locked()) {
			state.rmb_pending    = true;
			state.rmb_drag       = 0.0f;
			state.rmb_down_frame = sapp_frame_count();
			state.mouse_dx       = 0.0f;
			state.mouse_dy       = 0.0f;
		}
		break;

	case SAPP_EVENTTYPE_MOUSE_UP:

		if (event->mouse_button == SAPP_MOUSEBUTTON_LEFT) {
			state.mouse_left = false;
			state.mouse_left_release = true;
		}
		if (event->mouse_button == SAPP_MOUSEBUTTON_RIGHT) {
			state.mouse_right = false;
			state.mouse_right_release = true;
		}
		if (event->mouse_button == SAPP_MOUSEBUTTON_MIDDLE) {
			state.mouse_middle = false;
			state.mouse_middle_release = true;
		}

		if (event->mouse_button == SAPP_MOUSEBUTTON_RIGHT) {
			if (sapp_mouse_locked()) {
				sapp_lock_mouse(false);
				state.mouse_captured = false;
			}
			state.rmb_pending = false;
			state.rmb_drag    = 0.0f;
		}
		break;

	case SAPP_EVENTTYPE_MOUSE_MOVE:

	state.mouse_x = event->mouse_x;
	state.mouse_y = event->mouse_y;

	state.mouse_dx += event->mouse_dx;
	state.mouse_dy += event->mouse_dy;

	if (!sapp_mouse_locked()) {
		if (state.rmb_pending) {
			const float dx = event->mouse_dx;
			const float dy = event->mouse_dy;
			state.rmb_drag += dx*dx + dy*dy;

			const bool drag_ok = state.rmb_drag >= k_deadzone_px * k_deadzone_px;
			const bool hold_ok = (sapp_frame_count() - state.rmb_down_frame) >= k_hold_frames;

			if ((drag_ok || hold_ok) && !sapp_mouse_locked()) {
				sapp_lock_mouse(true);
				state.mouse_captured = true;
				state.mouse_dx       = 0.0f;
				state.mouse_dy       = 0.0f;
				state.rmb_pending    = false;
			}
		}
	}
	break;

	case SAPP_EVENTTYPE_MOUSE_SCROLL:
		state.scroll_x += event->scroll_x;
		state.scroll_y += event->scroll_y;
		break;

	case SAPP_EVENTTYPE_CHAR:
		state.characters.push_back((uint32_t)event->char_code);
		break;

	case SAPP_EVENTTYPE_KEY_DOWN: {

		if (event->key_code == SAPP_KEYCODE_1)  { state.key_1_press = true; }
		if (event->key_code == SAPP_KEYCODE_2)  { state.key_2_press = true; }
		if (event->key_code == SAPP_KEYCODE_3)  { state.key_3_press = true; }

		if (event->key_code == SAPP_KEYCODE_F1) { state.key_f1_press = true; }
		if (event->key_code == SAPP_KEYCODE_F2) { state.key_f2_press = true; }
		if (event->key_code == SAPP_KEYCODE_F3) { state.key_f3_press = true; }
		if (event->key_code == SAPP_KEYCODE_F4) { state.key_f4_press = true; }
		if (event->key_code == SAPP_KEYCODE_F5) { state.key_f5_press = true; }
		if (event->key_code == SAPP_KEYCODE_F6) { state.key_f6_press = true; }

		if (event->key_code == SAPP_KEYCODE_W)  { state.key_w = true; }
		if (event->key_code == SAPP_KEYCODE_A)  { state.key_a = true; }
		if (event->key_code == SAPP_KEYCODE_S)  { state.key_s = true; }
		if (event->key_code == SAPP_KEYCODE_D)  { state.key_d = true; }
		if (event->key_code == SAPP_KEYCODE_Q)  { state.key_q = true; }
		if (event->key_code == SAPP_KEYCODE_E)  { state.key_e = true; }

		if (event->key_code == SAPP_KEYCODE_ENTER)     { state.key_enter     = true; }
		if (event->key_code == SAPP_KEYCODE_TAB)       { state.key_tab       = true; }
		if (event->key_code == SAPP_KEYCODE_BACKSPACE) { state.key_backspace = true; }
		if (event->key_code == SAPP_KEYCODE_DELETE)    { state.key_delete    = true; }
		if (event->key_code == SAPP_KEYCODE_LEFT)      { state.key_left      = true; }
		if (event->key_code == SAPP_KEYCODE_RIGHT)     { state.key_right     = true; }
		if (event->key_code == SAPP_KEYCODE_UP)        { state.key_up        = true; }
		if (event->key_code == SAPP_KEYCODE_DOWN)      { state.key_down      = true; }
		if (event->key_code == SAPP_KEYCODE_HOME)      { state.key_home      = true; }
		if (event->key_code == SAPP_KEYCODE_END)       { state.key_end       = true; }

		const bool new_shift = (event->modifiers & SAPP_MODIFIER_SHIFT) != 0;

		if ( new_shift && !state.key_shift) state.key_shift_press   = true;
		if (!new_shift &&  state.key_shift) state.key_shift_release = true;

		state.key_shift = new_shift;
	
		state.key_ctrl  = (event->modifiers & SAPP_MODIFIER_CTRL)  != 0;
		state.key_alt   = (event->modifiers & SAPP_MODIFIER_ALT)   != 0;

		if (event->key_code == SAPP_KEYCODE_ESCAPE) {
			if (sapp_mouse_locked()) {
				sapp_lock_mouse(false);
				state.mouse_captured = false;
			}
			else {
				sapp_request_quit();
			}
			state.rmb_pending = false;
			state.rmb_drag    = 0.0f;
		}
	}
	break;

	case SAPP_EVENTTYPE_KEY_UP: {

		if (event->key_code == SAPP_KEYCODE_W) { state.key_w = false; }
		if (event->key_code == SAPP_KEYCODE_A) { state.key_a = false; }
		if (event->key_code == SAPP_KEYCODE_S) { state.key_s = false; }
		if (event->key_code == SAPP_KEYCODE_D) { state.key_d = false; }
		if (event->key_code == SAPP_KEYCODE_Q) { state.key_q = false; }
		if (event->key_code == SAPP_KEYCODE_E) { state.key_e = false; }

		if (event->key_code == SAPP_KEYCODE_ENTER)     { state.key_enter     = false; }
		if (event->key_code == SAPP_KEYCODE_TAB)       { state.key_tab       = false; }
		if (event->key_code == SAPP_KEYCODE_BACKSPACE) { state.key_backspace = false; }
		if (event->key_code == SAPP_KEYCODE_DELETE)    { state.key_delete    = false; }
		if (event->key_code == SAPP_KEYCODE_LEFT)      { state.key_left      = false; }
		if (event->key_code == SAPP_KEYCODE_RIGHT)     { state.key_right     = false; }
		if (event->key_code == SAPP_KEYCODE_UP)        { state.key_up        = false; }
		if (event->key_code == SAPP_KEYCODE_DOWN)      { state.key_down      = false; }
		if (event->key_code == SAPP_KEYCODE_HOME)      { state.key_home      = false; }
		if (event->key_code == SAPP_KEYCODE_END)       { state.key_end       = false; }

		const bool new_shift = (event->modifiers & SAPP_MODIFIER_SHIFT) != 0;

		if (!new_shift &&  state.key_shift) state.key_shift_release = true;
		if ( new_shift && !state.key_shift) state.key_shift_press  = true;

		state.key_shift = new_shift;

		state.key_ctrl  = (event->modifiers & SAPP_MODIFIER_CTRL)  != 0;
		state.key_alt   = (event->modifiers & SAPP_MODIFIER_ALT)   != 0;

	}
	break;

	case SAPP_EVENTTYPE_RESIZED:
		state.win_width  = event->window_width;
		state.win_height = event->window_height;
		break;

	case SAPP_EVENTTYPE_FOCUSED:
		state.focused = true;
		break;

	case SAPP_EVENTTYPE_UNFOCUSED:
		if (sapp_mouse_locked()) {
			sapp_lock_mouse(false);
			state.mouse_captured = false;
		}
		state.rmb_pending = false;
		state.rmb_drag    = 0.0f;
		state.focused     = false;
		break;

	default:
		break;
	}
}


struct InputBackend
{
	InputState* input_state {nullptr};

	void on_event(const sapp_event* event_ptr)
	{
		if (input_state)
			handle_event(event_ptr, *input_state);
	}
};

} // hpr::io

