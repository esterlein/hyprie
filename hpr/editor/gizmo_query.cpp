#include "gizmo_query.hpp"

#include <cmath>
#include <algorithm>

#include "math.hpp"


namespace hpr::edt {


struct GizmoBasis
{
	vec3 axis_x;
	vec3 axis_y;
	vec3 axis_z;
};

struct RayAxisProjection
{
	float closest_axis_coord;
	float distance;
};


static inline GizmoBasis build_gizmo_basis(TransformSpace space, const quat& rotation)
{
	GizmoBasis basis {
		{1.0f, 0.0f, 0.0f},
		{0.0f, 1.0f, 0.0f},
		{0.0f, 0.0f, 1.0f},
	};

	if (space == TransformSpace::Local) {
		basis.axis_x = glm::rotate(rotation, basis.axis_x);
		basis.axis_y = glm::rotate(rotation, basis.axis_y);
		basis.axis_z = glm::rotate(rotation, basis.axis_z);
	}

	basis.axis_x = glm::normalize(basis.axis_x);
	basis.axis_y = glm::normalize(basis.axis_y);
	basis.axis_z = glm::normalize(basis.axis_z);

	return basis;
}


static inline RayAxisProjection project_ray_onto_axis(
	const vec3& ray_origin,
	const vec3& ray_dir_unit,
	const vec3& axis_origin,
	const vec3& axis_dir_unit
)
{
	const vec3 origin_offset = ray_origin - axis_origin;

	const float ray_dot_ray     = glm::dot(ray_dir_unit,  ray_dir_unit);
	const float ray_dot_axis    = glm::dot(ray_dir_unit,  axis_dir_unit);
	const float axis_dot_axis   = glm::dot(axis_dir_unit, axis_dir_unit);

	const float ray_dot_offset  = glm::dot(ray_dir_unit,  origin_offset);
	const float axis_dot_offset = glm::dot(axis_dir_unit, origin_offset);

	const float denominator = ray_dot_ray * axis_dot_axis - ray_dot_axis * ray_dot_axis;

	float ray_coord  = 0.0f;
	float axis_coord = 0.0f;

	if (std::abs(denominator) > math::collinearity_epsilon) {
		ray_coord  = (ray_dot_axis * axis_dot_offset - axis_dot_axis * ray_dot_offset) / denominator;
		axis_coord = (ray_dot_ray  * axis_dot_offset - ray_dot_axis  * ray_dot_offset) / denominator;

		if (ray_coord < 0.0f) {
			ray_coord  = 0.0f;
			axis_coord = axis_dot_offset;
		}
	}
	else {
		ray_coord  = 0.0f;
		axis_coord = axis_dot_offset;
	}

	const vec3 point_on_axis = axis_origin + axis_dir_unit * axis_coord;
	const vec3 point_on_ray  = ray_origin  + ray_dir_unit  * ray_coord;

	const float distance = glm::length(point_on_axis - point_on_ray);

	return RayAxisProjection {axis_coord, distance};
}


static inline bool intersect_ray_with_plane(
	const vec3& ray_origin,
	const vec3& ray_dir_unit,
	const vec3& plane_origin,
	const vec3& plane_normal_unit,
	float&      hit_distance,
	vec3&       hit_point
)
{
	const float numerator   = glm::dot(plane_normal_unit, plane_origin - ray_origin);
	const float denominator = glm::dot(plane_normal_unit, ray_dir_unit);

	if (glm::epsilonEqual(denominator, 0.0f, math::collinearity_epsilon))
		return false;

	const float distance = numerator / denominator;
	if (distance < 0.0f)
		return false;

	hit_distance = distance;
	hit_point    = ray_origin + ray_dir_unit * distance;

	return true;
}


static inline bool point_in_plane_square(
	const vec3& point,
	const vec3& square_center,
	const vec3& axis_u_unit,
	const vec3& axis_v_unit,
	float       half_size
)
{
	const vec3 offset = point - square_center;

	const float u = glm::dot(offset, axis_u_unit);
	const float v = glm::dot(offset, axis_v_unit);

	return std::abs(u) <= half_size && std::abs(v) <= half_size;
}


HoverResult hover_translate(
	const scn::Ray&   pick_ray_world,
	const vec3&       gizmo_origin_world,
	const quat&       gizmo_rotation_world,
	TransformSpace    space,
	const GizmoStyle& style,
	float             world_units_per_pixel
)
{
	HoverResult result {};

	const vec3 ray_origin  = pick_ray_world.origin;
	const vec3 ray_dir     = glm::normalize(pick_ray_world.direction);
	const GizmoBasis basis = build_gizmo_basis(space, gizmo_rotation_world);

	const float axis_radius_world = world_units_per_pixel * style.axis_thick_px;
	const float plane_half_world  = 0.5f * world_units_per_pixel * style.plane_side_px;

	struct AxisCandidate
	{
		GizmoAxis axis;
		float     distance_px;
		float     closest_axis_coord;
	};

	AxisCandidate axis_candidate {
		GizmoAxis::Screen,
		std::numeric_limits<float>::max(),
		0.0f
	};

	{
		const RayAxisProjection closest = project_ray_onto_axis(ray_origin, ray_dir, gizmo_origin_world, basis.axis_x);
		const float dist_world = closest.distance;
		if (dist_world <= axis_radius_world) {
			const float px = dist_world / world_units_per_pixel;
			if (px < axis_candidate.distance_px)
				axis_candidate = {GizmoAxis::X, px, closest.closest_axis_coord};
		}
	}
	{
		const RayAxisProjection closest = project_ray_onto_axis(ray_origin, ray_dir, gizmo_origin_world, basis.axis_y);
		const float dist_world = closest.distance;
		if (dist_world <= axis_radius_world) {
			const float px = dist_world / world_units_per_pixel;
			if (px < axis_candidate.distance_px)
				axis_candidate = {GizmoAxis::Y, px, closest.closest_axis_coord};
		}
	}
	{
		const RayAxisProjection closest = project_ray_onto_axis(ray_origin, ray_dir, gizmo_origin_world, basis.axis_z);
		const float dist_world = closest.distance;
		if (dist_world <= axis_radius_world) {
			const float px = dist_world / world_units_per_pixel;
			if (px < axis_candidate.distance_px)
				axis_candidate = {GizmoAxis::Z, px, closest.closest_axis_coord};
		}
	}

	struct PlaneCandidate
	{
		GizmoAxis axis;
		float     distance_px;
		float     hit_distance;
	};

	PlaneCandidate best_plane {
		GizmoAxis::Screen,
		std::numeric_limits<float>::max(),
		0.0f
	};

	auto test_plane = [
		&ray_origin,
		&ray_dir,
		&gizmo_origin_world,
		&plane_half_world,
		&world_units_per_pixel,
		&best_plane
	](
		const vec3& axis_u,
		const vec3& axis_v,
		GizmoAxis   axis_label
	)
	{
		const vec3 plane_normal = glm::cross(axis_u, axis_v);

		float hit_distance = 0.0f;
		vec3  hit_point {};

		if (!intersect_ray_with_plane(ray_origin, ray_dir, gizmo_origin_world, plane_normal, hit_distance, hit_point))
			return;

		if (!point_in_plane_square(hit_point, gizmo_origin_world, axis_u, axis_v, plane_half_world))
			return;

		const float dist_world = glm::length(hit_point - gizmo_origin_world);
		const float px = dist_world / world_units_per_pixel;

		if (px < best_plane.distance_px)
			best_plane = {axis_label, px, hit_distance};
	};

	test_plane(basis.axis_x, basis.axis_y, GizmoAxis::XY);
	test_plane(basis.axis_x, basis.axis_z, GizmoAxis::XZ);
	test_plane(basis.axis_y, basis.axis_z, GizmoAxis::YZ);

	const bool has_axis  = axis_candidate.axis  != GizmoAxis::Screen;
	const bool has_plane = best_plane.axis != GizmoAxis::Screen;

	if (!has_axis && !has_plane)
		return result;

	if (has_axis && (!has_plane || axis_candidate.distance_px <= best_plane.distance_px * 0.9f)) {
		result.hit         = true;
		result.axis        = axis_candidate.axis;
		result.hit_param   = axis_candidate.closest_axis_coord;
		result.distance_px = axis_candidate.distance_px;
		return result;
	}

	result.hit         = true;
	result.axis        = best_plane.axis;
	result.hit_param   = best_plane.hit_distance;
	result.distance_px = best_plane.distance_px;

	return result;
}


DragResult solve_translate_drag(
	const scn::Ray& start_ray_world,
	const scn::Ray& current_ray_world,
	GizmoAxis       active_axis,
	const vec3&     gizmo_origin_world,
	const quat&     gizmo_rotation_world,
	TransformSpace  space
)
{
	DragResult drag_result {};

	const GizmoBasis basis = build_gizmo_basis(space, gizmo_rotation_world);

	const vec3 start_origin   = start_ray_world.origin;
	const vec3 start_dir      = glm::normalize(start_ray_world.direction);
	const vec3 current_origin = current_ray_world.origin;
	const vec3 current_dir    = glm::normalize(current_ray_world.direction);

	auto project_to_axis = [&gizmo_origin_world](
		const vec3& ray_origin,
		const vec3& ray_dir,
		const vec3& axis_dir,
		float&      axis_coord,
		vec3&       axis_point
	)
	{
		const RayAxisProjection ray_axis_proj = project_ray_onto_axis(ray_origin, ray_dir, gizmo_origin_world, axis_dir);
		axis_coord = ray_axis_proj.closest_axis_coord;
		axis_point = gizmo_origin_world + axis_dir * axis_coord;
	};

	auto project_to_plane = [&gizmo_origin_world](
		const vec3& ray_origin,
		const vec3& ray_dir,
		const vec3& axis_u,
		const vec3& axis_v,
		vec3&       hit_point,
		bool&       is_hit
	)
	{
		is_hit = false;

		const vec3 plane_normal = glm::cross(axis_u, axis_v);

		float hit_distance = 0.0f;
		vec3  intersection {};

		if (!intersect_ray_with_plane(ray_origin, ray_dir, gizmo_origin_world, plane_normal, hit_distance, intersection))
			return;

		hit_point = intersection;
		is_hit    = true;
	};

	switch (active_axis)
	{
	case GizmoAxis::X:
	case GizmoAxis::Y:
	case GizmoAxis::Z:
	{
		const vec3 axis_dir =
			(active_axis == GizmoAxis::X) ? basis.axis_x :
			(active_axis == GizmoAxis::Y) ? basis.axis_y :
			                                basis.axis_z;

		float start_coord   = 0.0f;
		float current_coord = 0.0f;

		vec3 start_point   {};
		vec3 current_point {};

		project_to_axis(start_origin,   start_dir,   axis_dir, start_coord,   start_point);
		project_to_axis(current_origin, current_dir, axis_dir, current_coord, current_point);

		drag_result.delta_translate = current_point - start_point;
		break;
	}

	case GizmoAxis::XY:
	case GizmoAxis::XZ:
	case GizmoAxis::YZ:
	{
		const vec3 axis_u =
			(active_axis == GizmoAxis::XY) ? basis.axis_x :
			(active_axis == GizmoAxis::XZ) ? basis.axis_x :
			                                 basis.axis_y;

		const vec3 axis_v =
			(active_axis == GizmoAxis::XY) ? basis.axis_y :
			(active_axis == GizmoAxis::XZ) ? basis.axis_z :
			                                 basis.axis_z;

		vec3 start_point   {};
		vec3 current_point {};

		bool is_start_hit   = false;
		bool is_current_hit = false;

		project_to_plane(start_origin,   start_dir,   axis_u, axis_v, start_point,   is_start_hit);
		project_to_plane(current_origin, current_dir, axis_u, axis_v, current_point, is_current_hit);

		if (is_start_hit && is_current_hit)
			drag_result.delta_translate = current_point - start_point;

		break;
	}

	default:
		break;
	}

	return drag_result;
}


HoverResult hover_rotate(
	const scn::Ray&   pick_ray_world,
	const vec3&       gizmo_origin_world,
	const quat&       gizmo_rotation_world,
	TransformSpace    space,
	const GizmoStyle& style,
	float             world_units_per_pixel
)
{
	HoverResult hover_result {};

	const vec3 ray_origin = pick_ray_world.origin;
	const vec3 ray_dir    = glm::normalize(pick_ray_world.direction);

	const GizmoBasis basis = build_gizmo_basis(space, gizmo_rotation_world);

	const float ring_radius_world    = style.ring_radius_px * world_units_per_pixel;
	const float half_thickness_world = 0.5f * style.ring_thick_px * world_units_per_pixel;

	struct AxisCandidate
	{
		GizmoAxis axis;
		float     ring_deviation_world;
	};

	AxisCandidate axis_candidate {
		GizmoAxis::Screen,
		std::numeric_limits<float>::max()
	};

	auto test_axis = [
		&ray_origin,
		&ray_dir,
		&gizmo_origin_world,
		ring_radius_world,
		half_thickness_world,
		&axis_candidate
	](
		const vec3& axis_dir,
		GizmoAxis   axis_label
	)
	{
		float hit_distance = 0.0f;
		vec3  hit_point {};

		if (!intersect_ray_with_plane(ray_origin, ray_dir, gizmo_origin_world, axis_dir, hit_distance, hit_point))
			return;

		const float radius_world   = glm::length(hit_point - gizmo_origin_world);
		const float ring_deviation = glm::abs(radius_world - ring_radius_world);

		if (ring_deviation <= half_thickness_world && ring_deviation < axis_candidate.ring_deviation_world) {
			axis_candidate = {axis_label, ring_deviation};
		}
	};

	test_axis(basis.axis_x, GizmoAxis::X);
	test_axis(basis.axis_y, GizmoAxis::Y);
	test_axis(basis.axis_z, GizmoAxis::Z);

	if (axis_candidate.axis == GizmoAxis::Screen)
		return hover_result;

	hover_result.hit         = true;
	hover_result.axis        = axis_candidate.axis;
	hover_result.hit_param   = 0.0f;
	hover_result.distance_px = axis_candidate.ring_deviation_world / world_units_per_pixel;
	return hover_result;
}


HoverResult hover_scale(
	const scn::Ray&   pick_ray_world,
	const vec3&       gizmo_origin_world,
	const quat&       gizmo_rotation_world,
	TransformSpace    space,
	const GizmoStyle& style,
	float             world_units_per_pixel
)
{
	HoverResult hover_result {};

	const vec3 ray_origin = pick_ray_world.origin;
	const vec3 ray_dir    = glm::normalize(pick_ray_world.direction);

	const GizmoBasis basis = build_gizmo_basis(space, gizmo_rotation_world);

	const float axis_len_world    = style.axis_len_px        * world_units_per_pixel;
	const float tip_half_world    = 0.5f * style.tip_cube_px * world_units_per_pixel;
	const float axis_radius_world = style.axis_thick_px      * world_units_per_pixel;

	struct AxisCandidate
	{
		GizmoAxis axis;
		float     distance_px;
		float     axis_coord;
	};

	AxisCandidate axis_candidate {
		GizmoAxis::Screen,
		std::numeric_limits<float>::max(),
		0.0f
	};

	auto test_axis_tip = [
		&ray_origin,
		&ray_dir,
		&gizmo_origin_world,
		axis_len_world,
		tip_half_world,
		world_units_per_pixel,
		&axis_candidate
	](
		const vec3& axis_dir,
		GizmoAxis   axis_label
	)
	{
		const RayAxisProjection ray_axis_proj = project_ray_onto_axis(
			ray_origin,
			ray_dir,
			gizmo_origin_world,
			axis_dir
		);

		const float dist_world = ray_axis_proj.distance;
		if (dist_world > tip_half_world)
			return;

		const float axis_coord = ray_axis_proj.closest_axis_coord;
		if (axis_coord < axis_len_world - tip_half_world || axis_coord > axis_len_world + tip_half_world)
			return;

		const float distance_px = dist_world / world_units_per_pixel;
		if (distance_px < axis_candidate.distance_px)
			axis_candidate = {axis_label, distance_px, axis_coord};
	};

	test_axis_tip(basis.axis_x, GizmoAxis::X);
	test_axis_tip(basis.axis_y, GizmoAxis::Y);
	test_axis_tip(basis.axis_z, GizmoAxis::Z);

	if (axis_candidate.axis != GizmoAxis::Screen) {
		hover_result.hit         = true;
		hover_result.axis        = axis_candidate.axis;
		hover_result.hit_param   = axis_candidate.axis_coord;
		hover_result.distance_px = axis_candidate.distance_px;
		return hover_result;
	}

	struct PlaneCandidate
	{
		GizmoAxis axis;
		float     distance_px;
	};

	PlaneCandidate plane_candidate {
		GizmoAxis::Screen,
		std::numeric_limits<float>::max()
	};

	auto test_plane_uniform = [
		&ray_origin,
		&ray_dir,
		&gizmo_origin_world,
		axis_len_world,
		tip_half_world,
		world_units_per_pixel,
		&plane_candidate
	](
		const vec3& plane_u,
		const vec3& plane_v,
		GizmoAxis   axis_label
	)
	{
		const vec3 plane_normal = glm::normalize(glm::cross(plane_u, plane_v));

		float hit_distance = 0.0f;
		vec3  hit_point {};

		if (!intersect_ray_with_plane(ray_origin, ray_dir, gizmo_origin_world, plane_normal, hit_distance, hit_point))
			return;

		const float dist_world = glm::length(hit_point - gizmo_origin_world);
		if (dist_world > axis_len_world + tip_half_world)
			return;

		const float distance_px = glm::abs(dist_world - axis_len_world) / world_units_per_pixel;
		if (distance_px < plane_candidate.distance_px)
			plane_candidate = {axis_label, distance_px};
	};

	test_plane_uniform(basis.axis_x, basis.axis_y, GizmoAxis::XY);
	test_plane_uniform(basis.axis_x, basis.axis_z, GizmoAxis::XZ);
	test_plane_uniform(basis.axis_y, basis.axis_z, GizmoAxis::YZ);

	if (plane_candidate.axis == GizmoAxis::Screen)
		return hover_result;

	hover_result.hit         = true;
	hover_result.axis        = plane_candidate.axis;
	hover_result.hit_param   = 0.0f;
	hover_result.distance_px = plane_candidate.distance_px;

	return hover_result;
}


} // hpr::edt

