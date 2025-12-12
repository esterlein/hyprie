#pragma once

#include <cmath>

#include "sokol_app.h"

#include "math.hpp"
#include "input_state.hpp"


namespace hpr::scn {


class Camera
{
public:

	Camera();
	void set_perspective(float fov_y_deg, float aspect, float znear, float zfar);
	void update_view();
	void look_delta(float dx, float dy);
	void move(bool fwd, bool back, bool left, bool right, bool up, bool down, float delta_time);
	bool update_input(const hpr::io::InputState& input_state, float delta_time);

	vec3 position;

	float yaw;
	float pitch;
	float move_speed;
	float mouse_sens;

	mat4 mtx_V  {1.0f};
	mat4 mtx_P  {1.0f};
	mat4 mtx_VP {1.0f};

private:

	bool m_capture  {false};
	bool m_prev_rmb {false};
};

} // hpr::scn

