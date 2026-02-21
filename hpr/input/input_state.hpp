#pragma once

#include <cstdint>

#include "mtp_memory.hpp"


namespace hpr::io {


struct InputState
{
	bool key_1_press {false};
	bool key_2_press {false};
	bool key_3_press {false};

	bool key_w {false};
	bool key_a {false};
	bool key_s {false};
	bool key_d {false};
	bool key_q {false};
	bool key_e {false};

	bool key_g {false};

	float mouse_dx {0.0f};
	float mouse_dy {0.0f};

	int win_width  {1};
	int win_height {1};

	bool mouse_captured {false};

	float mouse_x {0.0f};
	float mouse_y {0.0f};

	bool mouse_left   {false};
	bool mouse_right  {false};
	bool mouse_middle {false};

	bool mouse_left_press     {false};
	bool mouse_left_release   {false};
	bool mouse_right_press    {false};
	bool mouse_right_release  {false};
	bool mouse_middle_press   {false};
	bool mouse_middle_release {false};

	float mouse_press_x {0.0f};
	float mouse_press_y {0.0f};

	bool     rmb_pending    {false};
	float    rmb_drag       {0.0f};
	uint64_t rmb_down_frame {0};

	float scroll_x {0.0f};
	float scroll_y {0.0f};

	bool key_shift {false};
	bool key_ctrl  {false};
	bool key_alt   {false};

	bool key_shift_press   {false};
	bool key_shift_release {false};

	bool key_esc       {false};
	bool key_enter     {false};
	bool key_tab       {false};
	bool key_backspace {false};
	bool key_delete    {false};
	bool key_left      {false};
	bool key_right     {false};
	bool key_up        {false};
	bool key_down      {false};
	bool key_home      {false};
	bool key_end       {false};

	bool key_f1_press {false};
	bool key_f2_press {false};
	bool key_f3_press {false};
	bool key_f4_press {false};
	bool key_f5_press {false};
	bool key_f6_press {false};

	bool key_f9_press {false};

	bool focused {true};

	mtp::vault<uint32_t, mtp::default_set> characters;

	void clear_mouse_delta()
	{
		mouse_dx = 0.0f;
		mouse_dy = 0.0f;
	}

	void clear_ui_frame()
	{
		scroll_x = 0.0f;
		scroll_y = 0.0f;

		characters.clear();

		key_1_press = false;
		key_2_press = false;
		key_3_press = false;

		mouse_left_press     = false;
		mouse_left_release   = false;
		mouse_right_press    = false;
		mouse_right_release  = false;
		mouse_middle_press   = false;
		mouse_middle_release = false;

		key_shift_press   = false;
		key_shift_release = false;

		key_f1_press = false;
		key_f2_press = false;
		key_f3_press = false;
		key_f4_press = false;
		key_f5_press = false;
		key_f6_press = false;
		key_f9_press = false;
	}
};

} // hpr::io

