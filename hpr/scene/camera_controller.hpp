#pragma once


namespace hpr::scn {


class CameraController
{
public:

	CameraController()
		: yaw        {0.0f}
		, pitch      {0.0f}
		, move_speed {40.0f}
		, mouse_sens {0.0025f}
	{}

	float yaw;
	float pitch;
	float move_speed;
	float mouse_sens;

	void look_delta(float dx, float dy)
	{
		yaw   -= dx * mouse_sens;
		pitch -= dy * mouse_sens;

		if (pitch >  1.55f) pitch =  1.55f;
		if (pitch < -1.55f) pitch = -1.55f;
	}

	enum class Mode
	{
		fly,
		iso
	} mode {Mode::iso};

	struct DeltaInput
	{
		float orbit_x {0.0f};
		float orbit_y {0.0f};

		float pan_x {0.0f};
		float pan_y {0.0f};
		float dolly {0.0f};

		float move_forward {0.0f};
		float move_right   {0.0f};
		float move_up      {0.0f};
	} delta;

	float iso_ortho_height {40.0f};
	float iso_pitch        {-0.6154797f}; // atan(1 / sqrt(2))
	float iso_yaw          {0.7853982f};  // pi / 4

	float iso_min_depth_span   {200.0f};
	float iso_depth_multiplier {8.0f};
};

} // hpr::scn
