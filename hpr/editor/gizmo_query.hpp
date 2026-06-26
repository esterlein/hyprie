#pragma once

#include <cstdint>
#include "math.hpp"
#include "action.hpp"
#include "editor_data.hpp"
#include "scene_query.hpp"


namespace hpr::edt {


struct HoverResult
{
	bool      hit         {false};
	GizmoAxis axis        {GizmoAxis::Screen};
	float     hit_param   {0.0f};
	float     distance_px {0.0f};
};

struct DragResult
{
	vec3  delta_translate {0.0f, 0.0f, 0.0f};
	float delta_angle_rad {0.0f};
	vec3  delta_scale     {0.0f, 0.0f, 0.0f};
};

HoverResult hover_translate(
	const scn::Ray&   pick_ray_world,
	const vec3&       gizmo_origin_world,
	const quat&       gizmo_rotation_world,
	TransformSpace    space,
	const GizmoStyle& style,
	float             world_units_per_pixel
);

DragResult solve_translate_drag(
	const scn::Ray& start_ray_world,
	const scn::Ray& current_ray_world,
	GizmoAxis       active_axis,
	const vec3&     gizmo_origin_world,
	const quat&     gizmo_rotation_world,
	TransformSpace  space
);

HoverResult hover_rotate(
	const scn::Ray&   pick_ray_world,
	const vec3&       gizmo_origin_world,
	const quat&       gizmo_rotation_world,
	TransformSpace    space,
	const GizmoStyle& style,
	float             world_units_per_pixel
);

HoverResult hover_scale(
	const scn::Ray&   pick_ray_world,
	const vec3&       gizmo_origin_world,
	const quat&       gizmo_rotation_world,
	TransformSpace    space,
	const GizmoStyle& style,
	float             world_units_per_pixel
);

} // hpr::edt

