#pragma once

#include <cstdint>
#include <variant>

#include "editor_data.hpp"


namespace hpr {


enum class ActionKind : uint8_t
{
	Orbit,
	Pan,
	Dolly,
	FrameSelection,
	SelectClick,
	SelectMarqueeBegin,
	SelectMarqueeUpdate,
	SelectMarqueeEnd,
	GizmoBegin,
	GizmoUpdate,
	GizmoEnd,
	GizmoSetTranslate,
	GizmoSetRotate,
	GizmoSetScale,
	DebugToggleOverlay,
	DebugCycleLogLevel,
	DebugToggleCore,
	DebugToggleRender,
	DebugToggleScene,
	DebugToggleAsset,
	SnapOn,
	SnapOff,
	Move
};

struct MoveAction
{
	float forward;
	float right;
	float up;
};


struct OrbitAction
{
	float delta_x;
	float delta_y;
};

struct PanAction
{
	float delta_x;
	float delta_y;
};

struct DollyAction
{
	float amount;
};

struct FrameSelectionAction
{
};

struct SelectClickAction
{
	float x;
	float y;
	bool additive;
};

struct SelectMarqueeBeginAction
{
	float x;
	float y;
};

struct SelectMarqueeUpdateAction
{
	float x;
	float y;
};

struct SelectMarqueeEndAction
{
	float x;
	float y;
	bool additive;
};

struct GizmoBeginAction
{
	edt::GizmoAxis axis;
};

struct GizmoUpdateAction
{
	float delta_x;
	float delta_y;
	bool snapping;
};

struct GizmoEndAction
{};

struct GizmoSetTranslateAction
{};

struct GizmoSetRotateAction
{};

struct GizmoSetScaleAction
{};

struct DebugToggleOverlayAction
{};

struct DebugCycleLogLevelAction
{};

struct DebugToggleCoreAction
{};

struct DebugToggleRenderAction
{};

struct DebugToggleSceneAction
{};

struct DebugToggleAssetAction
{};

struct SnapOnAction
{};

struct SnapOffAction
{};

using ActionPayload = std::variant <
	OrbitAction,
	PanAction,
	DollyAction,
	FrameSelectionAction,
	SelectClickAction,
	SelectMarqueeBeginAction,
	SelectMarqueeUpdateAction,
	SelectMarqueeEndAction,
	GizmoBeginAction,
	GizmoUpdateAction,
	GizmoEndAction,
	GizmoSetTranslateAction,
	GizmoSetRotateAction,
	GizmoSetScaleAction,
	DebugToggleOverlayAction,
	DebugCycleLogLevelAction,
	DebugToggleCoreAction,
	DebugToggleRenderAction,
	DebugToggleSceneAction,
	DebugToggleAssetAction,
	SnapOnAction,
	SnapOffAction,
	MoveAction
>;

struct Action
{
	ActionKind kind;
	ActionPayload payload;
};

} // hpr

