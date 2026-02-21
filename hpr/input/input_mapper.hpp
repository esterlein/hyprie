#pragma once

#include <cmath>

#include "mtp_memory.hpp"

#include "log.hpp"
#include "action.hpp"
#include "input_state.hpp"
#include "input_binding.hpp"


namespace hpr::io {


class InputMapper
{
public:

	explicit InputMapper(const InputBinding& binding)
		: m_binding {binding}
	{}

	void map(const hpr::io::InputState& input_state, mtp::vault<Action, mtp::default_set>& current_actions)
	{
		if (input_state.scroll_y != 0.0f) {

			DollyAction payload {input_state.scroll_y};
			current_actions.push_back({ActionKind::Dolly, payload});
		}

		const bool pan_combo =
			input_state.mouse_middle ||
			(m_binding.pan_mmb_shift_rmb && input_state.key_shift && input_state.mouse_right);

		const bool orbit_combo =
			m_binding.orbit_rmb     &&
			input_state.mouse_right &&
			!input_state.key_shift  &&
			!input_state.mouse_middle;

		if (pan_combo && (input_state.mouse_dx != 0.0f || input_state.mouse_dy != 0.0f)) {
			const float sx = input_state.mouse_dx;
			const float sy = input_state.mouse_dy;

			PanAction payload {sx, sy};
			current_actions.push_back({ActionKind::Pan, payload});
		}
		else if (orbit_combo && (input_state.mouse_dx != 0.0f || input_state.mouse_dy != 0.0f)) {
			const float sx = input_state.mouse_dx;
			const float sy = input_state.mouse_dy;

			OrbitAction payload {sx, sy};
			current_actions.push_back({ActionKind::Orbit, payload});
		}

		if (input_state.key_shift_press) {

			SnapOnAction payload {};
			current_actions.push_back({ActionKind::SnapOn, payload});
		}
		if (input_state.key_shift_release) {

			SnapOffAction payload {};
			current_actions.push_back({ActionKind::SnapOff, payload});
		}


		if (input_state.key_1_press) {

			GizmoSetTranslateAction payload {};
			current_actions.push_back({ActionKind::GizmoSetTranslate, payload});
		}
		if (input_state.key_2_press) {

			GizmoSetRotateAction payload {};
			current_actions.push_back({ActionKind::GizmoSetRotate, payload});
		}
		if (input_state.key_3_press) {

			GizmoSetScaleAction payload {};
			current_actions.push_back({ActionKind::GizmoSetScale, payload});
		}


		if (input_state.key_f1_press) {
			DebugToggleOverlayAction payload {};
			current_actions.push_back({ActionKind::DebugToggleOverlay, payload});
		}
		if (input_state.key_f2_press) {
			DebugCycleLogLevelAction payload {};
			current_actions.push_back({ActionKind::DebugCycleLogLevel, payload});
		}
		if (input_state.key_f3_press) {
			DebugToggleCoreAction payload {};
			current_actions.push_back({ActionKind::DebugToggleCore, payload});
		}
		if (input_state.key_f4_press) {
			DebugToggleRenderAction payload {};
			current_actions.push_back({ActionKind::DebugToggleRender, payload});
		}
		if (input_state.key_f5_press) {
			DebugToggleSceneAction payload {};
			current_actions.push_back({ActionKind::DebugToggleScene, payload});
		}
		if (input_state.key_f6_press) {
			DebugToggleAssetAction payload {};
			current_actions.push_back({ActionKind::DebugToggleAsset, payload});
		}
		if (input_state.key_f9_press) {
			ToggleCameraModeAction payload {};
			current_actions.push_back({ActionKind::ToggleCameraMode, payload});
		}


		float move_forward = (input_state.key_w ? 1.0f : 0.0f) - (input_state.key_s ? 1.0f : 0.0f);
		float move_right   = (input_state.key_d ? 1.0f : 0.0f) - (input_state.key_a ? 1.0f : 0.0f);
		float move_up      = (input_state.key_e ? 1.0f : 0.0f) - (input_state.key_q ? 1.0f : 0.0f);

		if (move_forward != 0.0f || move_right != 0.0f || move_up != 0.0f) {

			MoveAction payload {move_forward, move_right, move_up};
			current_actions.push_back({ActionKind::Move, payload});
		}

		if (input_state.mouse_left_press) {

			SelectClickAction payload {input_state.mouse_x, input_state.mouse_y, input_state.key_shift};
			current_actions.push_back({ActionKind::SelectClick, payload});
		}

		if (input_state.mouse_left && (input_state.mouse_dx != 0.0f || input_state.mouse_dy != 0.0f)) {

			GizmoUpdateAction payload {input_state.mouse_dx, input_state.mouse_dy, input_state.key_shift};
			current_actions.push_back({ActionKind::GizmoUpdate, payload});
		}
		if (input_state.mouse_left_release) {

			GizmoEndAction payload {};
			current_actions.push_back({ActionKind::GizmoEnd, payload});
		}
	}

private:

	InputBinding m_binding;
};

} // hpr::io

