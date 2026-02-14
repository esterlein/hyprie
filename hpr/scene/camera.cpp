#include "camera.hpp"


namespace hpr::scn {


Camera::Camera()
	: position   {0.0f, 0.0f, 0.0f}
	, yaw        {0.0f}
	, pitch      {0.0f}
	, move_speed {40.0f}
	, mouse_sens {0.0025f}
{}


void Camera::set_perspective(float fov_y_deg, float aspect, float znear, float zfar)
{
	mtx_P = glm::perspective(glm::radians(fov_y_deg), aspect, znear, zfar);
	mtx_VP = mtx_P * mtx_V;
}


void Camera::update_view()
{
	const float sin_y = std::sin(yaw);
	const float cos_y = std::cos(yaw);
	const float sin_p = std::sin(pitch);
	const float cos_p = std::cos(pitch);

	const vec3 dir {sin_y * cos_p, sin_p, -cos_y * cos_p};
	const vec3 center = position + glm::normalize(dir);
	const vec3 up {0.0f, 1.0f, 0.0f};

	mtx_V = glm::lookAt(position, center, up);
	mtx_VP = mtx_P * mtx_V;
}


void Camera::look_delta(float dx, float dy)
{
	yaw   += dx * mouse_sens;
	pitch -= dy * mouse_sens;

	if (pitch >  1.55f) pitch =  1.55f;
	if (pitch < -1.55f) pitch = -1.55f;
}


void Camera::move(bool fwd, bool back, bool left, bool right, bool up, bool down, float delta_time)
{
	if (!(fwd || back || left || right || up || down) || delta_time <= 0.0f)
		return;

	const float sin_yaw   = std::sin(yaw);
	const float cos_yaw   = std::cos(yaw);
	const float sin_pitch = std::sin(pitch);
	const float cos_pitch = std::cos(pitch);

	vec3 forward = glm::normalize(vec3 {sin_yaw * cos_pitch, sin_pitch, -cos_yaw * cos_pitch});

	const vec3 world_up {0.0f, 1.0f, 0.0f};

	vec3 right_vec = glm::cross(forward, world_up);
	if (glm::dot(right_vec, right_vec) < 1e-8f) {
		right_vec = {cos_yaw, 0.0f,  sin_yaw};
	}
	else {
		right_vec = glm::normalize(right_vec);
	}

	vec3 move_delta {0.0f, 0.0f, 0.0f};
	const float move_step = move_speed * delta_time;

	if (fwd)   move_delta   += forward   *  move_step;
	if (back)  move_delta   += forward   * -move_step;
	if (right) move_delta   += right_vec *  move_step;
	if (left)  move_delta   += right_vec * -move_step;
	if (up)    move_delta.y += move_step;
	if (down)  move_delta.y -= move_step;

	position += move_delta;
}


bool Camera::update_input(const io::InputState& input_state, float delta_time)
{
	const bool rmb = input_state.mouse_right;
	const bool pressed = rmb && !m_prev_rmb;
	const bool released = !rmb && m_prev_rmb;

	if (pressed) {
		sapp_lock_mouse(true);
		m_capture = true;
	}
	if (released || input_state.key_esc || !input_state.focused || !sapp_mouse_locked()) {
		sapp_lock_mouse(false);
		m_capture = false;
	}

	m_prev_rmb = rmb;

	if (m_capture)
		look_delta(input_state.mouse_dx, input_state.mouse_dy);

	move(
		input_state.key_w,
		input_state.key_s,
		input_state.key_a,
		input_state.key_d,
		input_state.key_e,
		input_state.key_q,
		delta_time
	);

	return m_capture ||
		input_state.key_w ||
		input_state.key_s ||
		input_state.key_a ||
		input_state.key_d ||
		input_state.key_e ||
		input_state.key_q;
}

} // hpr::scn

