#include "gizmo_layer.hpp"

#include "math.hpp"
#include "event.hpp"
#include "action.hpp"
#include "renderer.hpp"
#include "editor_data.hpp"
#include "scene_query.hpp"
#include "gizmo_query.hpp"
#include "event_dispatcher.hpp"

#include "panic.hpp"


namespace hpr {


GizmoLayer::GizmoLayer(rdr::Renderer& renderer, edt::GizmoPrimitives gizmo_primitives)
	: m_renderer         {renderer}
	, m_gizmo_primitives {std::move(gizmo_primitives)}
{}


void GizmoLayer::on_attach()
{}


void GizmoLayer::on_detach()
{}


bool GizmoLayer::on_event(Event& event)
{
	EventDispatcher dispatcher(event);

	dispatcher.dispatch<SelectionChangedEvent>(
		[this](const SelectionChangedEvent& selection_event) -> bool
		{
			m_entity   = selection_event.selection.entity;
			m_position = selection_event.selection.transform.position;
			m_rotation = selection_event.selection.transform.rotation;
			m_scale    = selection_event.selection.transform.scale;

			m_active = false;
			m_active_axis = edt::GizmoAxis::None;

			return false;
		}
	);

	return false;
}


bool GizmoLayer::on_actions(std::span<const Action> actions)
{
	for (const Action& action : actions) {

		switch (action.kind) {

		case ActionKind::SelectClick:
		{
			const auto& payload = std::get<SelectClickAction>(action.payload);

			if (m_entity == ecs::invalid_entity)
				break;

			const auto surf = m_renderer.surface_info();
			const auto& view = m_renderer.frame_context().scene_view;

			const scn::Ray ray = scn::make_pick_ray(
				payload.x, payload.y,
				surf.width, surf.height,
				view
			);

			edt::HoverResult hover_result {};

			switch (m_mode) {
			case edt::GizmoMode::Translate:
				hover_result = edt::hover_translate(
					ray,
					m_position,
					m_rotation,
					m_space,
					m_gizmo_primitives.style,
					m_screen_scale
				);
				break;
			case edt::GizmoMode::Rotate:
				hover_result = edt::hover_rotate(
					ray,
					m_position,
					m_rotation,
					m_space,
					m_gizmo_primitives.style,
					m_screen_scale
				);
				break;
			case edt::GizmoMode::Scale:
				hover_result = edt::hover_scale(
					ray,
					m_position,
					m_rotation,
					m_space,
					m_gizmo_primitives.style,
					m_screen_scale
				);
				break;
			}

			if (!hover_result.hit || hover_result.axis == edt::GizmoAxis::None)
				break;

			m_hover_axis       = hover_result.axis;
			m_active           = true;
			m_active_axis      = m_hover_axis;
			m_drag_start_px    = {payload.x, payload.y};
			m_drag_accum_px    = {0.0f, 0.0f};
			m_drag_start_pos   = m_position;
			m_drag_start_rot   = m_rotation;
			m_drag_start_scale = m_scale;

			return true;
		}

		case ActionKind::GizmoUpdate:
		{
			if (!m_active || m_entity == ecs::invalid_entity)
				break;

			HPR_ASSERT(m_cmd_stream);

			const auto& payload = std::get<GizmoUpdateAction>(action.payload);
			m_snapping = payload.snapping;

			vec3 axis_x = {1.0f, 0.0f, 0.0f};
			vec3 axis_y = {0.0f, 1.0f, 0.0f};
			vec3 axis_z = {0.0f, 0.0f, 1.0f};

			if (m_space == edt::TransformSpace::Local) {
				axis_x = glm::rotate(m_rotation, axis_x);
				axis_y = glm::rotate(m_rotation, axis_y);
				axis_z = glm::rotate(m_rotation, axis_z);
			}

			m_drag_accum_px.x += payload.delta_x;
			m_drag_accum_px.y += payload.delta_y;

			float dx = m_drag_accum_px.x * m_screen_scale;
			float dy = m_drag_accum_px.y * m_screen_scale;

			if (m_snapping) {
				const float step = 1.0f;
				dx = std::round(dx / step) * step;
				dy = std::round(dy / step) * step;
			}

			const auto surface_info  = m_renderer.surface_info();
			const auto frame_context = m_renderer.frame_context();

			auto world_to_px = [&frame_context, &surface_info](const vec3& position_world) -> vec2
			{
				vec4 clip = frame_context.scene_view.mtx_VP * vec4(position_world, 1.0f);
				vec3 ndc_coords = vec3(clip) / clip.w;

				return vec2 {
					(ndc_coords.x * 0.5f + 0.5f)          * static_cast<float>(surface_info.width),
					(1.0f - (ndc_coords.y * 0.5f + 0.5f)) * static_cast<float>(surface_info.height)
				};
			};

			auto axis_screen = [&world_to_px](const vec3& origin_world, const vec3& axis_world)
			{
				return world_to_px(origin_world + axis_world) - world_to_px(origin_world);
			};

			auto pixels_to_axis_components = [](
				const vec2& delta_pixels,
				const vec2& axis_a_screen,
				const vec2& axis_b_screen
			)
			{
				const float determinant = axis_a_screen.x * axis_b_screen.y - axis_a_screen.y * axis_b_screen.x;

				if (glm::abs(determinant) < math::determinant_tolerance)
					return vec2 {0.0f, 0.0f};

				const float inv_determinant = 1.0f / determinant;

				const float component_a =
					( delta_pixels.x * axis_b_screen.y - delta_pixels.y * axis_b_screen.x) * inv_determinant;
				const float component_b =
					(-delta_pixels.x * axis_a_screen.y + delta_pixels.y * axis_a_screen.x) * inv_determinant;

				return vec2{component_a, component_b};
			};

			auto project_pixels_axis = [](const vec2& delta_pixels, const vec2& axis_screen_vec)
			{
				const float axis_length_pixels_sq = glm::dot(axis_screen_vec, axis_screen_vec);

				if (axis_length_pixels_sq < math::screen_len_sq_epsilon)
					return 0.0f;

				return (delta_pixels.x * axis_screen_vec.x + delta_pixels.y * axis_screen_vec.y) / axis_length_pixels_sq;
			};

			const vec2 drag_delta_px = m_drag_accum_px;
			const float step = 1.0f;

			const float ring_radius_px = m_gizmo_primitives.style.ring_radius_px * m_screen_scale;
			const float axis_len_px    = m_gizmo_primitives.style.axis_len_px    * m_screen_scale;

			if (m_mode == edt::GizmoMode::Translate) {

				vec3 translation_delta {};

				auto axis_translation_delta =
				[
					this,
					&project_pixels_axis,
					&drag_delta_px,
					step
				](const vec3& axis_dir, const vec2& axis_dir_screen) -> vec3
				{
					float distance_along_axis = project_pixels_axis(drag_delta_px, axis_dir_screen);

					if (m_snapping) {
						distance_along_axis = std::round(distance_along_axis / step) * step;
					}

					return axis_dir * distance_along_axis;
				};

				auto plane_translation_delta =
				[
					this,
					&pixels_to_axis_components,
					&drag_delta_px,
					step
				](
					const vec3& axis_u,
					const vec3& axis_v,
					const vec2& axis_u_screen,
					const vec2& axis_v_screen
				) -> vec3
				{
					vec2 components = pixels_to_axis_components(drag_delta_px, axis_u_screen, axis_v_screen);

					if (m_snapping) {
						components.x = std::round(components.x / step) * step;
						components.y = std::round(components.y / step) * step;
					}

					return axis_u * components.x + axis_v * components.y;
				};

				switch (m_active_axis) {
				case edt::GizmoAxis::X: {
					const vec2 axis_x_screen = axis_screen(m_drag_start_pos, axis_x);

					translation_delta = axis_translation_delta(axis_x, axis_x_screen);
				}
				break;
				case edt::GizmoAxis::Y: {
					const vec2 axis_y_screen = axis_screen(m_drag_start_pos, axis_y);

					translation_delta = axis_translation_delta(axis_y, axis_y_screen);
				}
				break;
				case edt::GizmoAxis::Z: {
					const vec2 axis_z_screen = axis_screen(m_drag_start_pos, axis_z);

					translation_delta = axis_translation_delta(
						axis_z,
						axis_z_screen
					);
				}
				break;
				case edt::GizmoAxis::XY: {
					const vec2 axis_x_screen = axis_screen(m_drag_start_pos, axis_x);
					const vec2 axis_y_screen = axis_screen(m_drag_start_pos, axis_y);

					translation_delta = plane_translation_delta(
						axis_x,
						axis_y,
						axis_x_screen,
						axis_y_screen
					);
				}
				break;
				case edt::GizmoAxis::XZ: {
					const vec2 axis_x_screen = axis_screen(m_drag_start_pos, axis_x);
					const vec2 axis_z_screen = axis_screen(m_drag_start_pos, axis_z);

					translation_delta = plane_translation_delta(
						axis_x,
						axis_z,
						axis_x_screen,
						axis_z_screen
					);
				}
				break;
				case edt::GizmoAxis::YZ: {
					const vec2 axis_y_screen = axis_screen(m_drag_start_pos, axis_y);
					const vec2 axis_z_screen = axis_screen(m_drag_start_pos, axis_z);

					translation_delta = plane_translation_delta(
						axis_y,
						axis_z,
						axis_y_screen,
						axis_z_screen
					);
				}
				break;
				case edt::GizmoAxis::All:
				{
					const mat3 inv_view3 = mat3(glm::inverse(frame_context.scene_view.mtx_V));

					const vec3 cam_right = normalize(vec3(inv_view3[0][0], inv_view3[0][1], inv_view3[0][2]));
					const vec3 cam_up    = normalize(vec3(inv_view3[1][0], inv_view3[1][1], inv_view3[1][2]));

					const vec2 cam_right_screen = axis_screen(m_drag_start_pos, cam_right);
					const vec2 cam_up_screen    = axis_screen(m_drag_start_pos, cam_up);

					translation_delta = plane_translation_delta(
						cam_right,
						cam_up,
						cam_right_screen,
						cam_up_screen
					);
				}
				break;
				}

				vec3 new_position = m_drag_start_pos + translation_delta;

				SetTransform transform_cmd {
					.entity             = m_entity,
					.transform.position = new_position,
					.transform.rotation = m_rotation,
					.transform.scale    = m_scale
				};

				m_cmd_stream->push(CmdType::SetTransform, transform_cmd);
				m_position = new_position;
				return true;
			}

			if (m_mode == edt::GizmoMode::Rotate) {

				const auto surface     = m_renderer.surface_info();
				const auto& scene_view = m_renderer.frame_context().scene_view;

				const scn::Ray start_ray = scn::make_pick_ray(
					m_drag_start_px.x,
					m_drag_start_px.y,
					surface.width,
					surface.height,
					scene_view
				);

				const vec2 curr_px = m_drag_start_px + m_drag_accum_px;

				const scn::Ray curr_ray = scn::make_pick_ray(
					curr_px.x,
					curr_px.y,
					surface.width,
					surface.height,
					scene_view
				);

				auto intersect_plane = [](
					const vec3& ray_origin,
					const vec3& ray_dir_unit,
					const vec3& plane_point,
					const vec3& plane_normal_unit,
					vec3&       intersection_point
				) -> bool
				{
					const float denominator = glm::dot(plane_normal_unit, ray_dir_unit);

					if (glm::abs(denominator) < math::determinant_tolerance) {
						return false;
					}

					const float distance_along_ray = glm::dot(plane_normal_unit, plane_point - ray_origin) / denominator;

					if (distance_along_ray < 0.0f) {
						return false;
					}

					intersection_point = ray_origin + ray_dir_unit * distance_along_ray;

					return true;
				};

				const float snap_step = glm::radians(5.0f);

				auto apply_axis_rotation =
				[
					this,
					&intersect_plane,
					&world_to_px,
					&start_ray,
					&curr_ray,
					&curr_px,
					&scene_view,
					snap_step
				](const vec3& axis_dir_unit) -> bool
				{
					vec3 start_hit_point {};
					vec3 curr_hit_point  {};

					const bool start_hit_valid = intersect_plane(
						start_ray.origin,
						glm::normalize(start_ray.direction),
						m_drag_start_pos,
						axis_dir_unit,
						start_hit_point
					);

					const bool curr_hit_valid = intersect_plane(
						curr_ray.origin,
						glm::normalize(curr_ray.direction),
						m_drag_start_pos,
						axis_dir_unit,
						curr_hit_point
					);

					float angle_delta = 0.0f;
					if (start_hit_valid && curr_hit_valid) {

						const vec3 start_dir_unit = glm::normalize(start_hit_point - m_drag_start_pos);
						const vec3 curr_dir_unit  = glm::normalize(curr_hit_point  - m_drag_start_pos);

						const float cos_angle_delta = glm::clamp(glm::dot(start_dir_unit, curr_dir_unit), -1.0f, 1.0f);
						const float sin_angle_delta = glm::dot(axis_dir_unit, glm::cross(start_dir_unit, curr_dir_unit));

						angle_delta = std::atan2(sin_angle_delta, cos_angle_delta);
					}
					else {
						const vec2 center_px = world_to_px(m_drag_start_pos);

						const vec2 start_delta_px = m_drag_start_px - center_px;
						const vec2 curr_delta_px = curr_px - center_px;

						const float cos_angle_delta =
							glm::clamp(glm::dot(glm::normalize(start_delta_px), glm::normalize(curr_delta_px)), -1.0f, 1.0f);
						const float sin_angle_delta =
							start_delta_px.x * curr_delta_px.y - start_delta_px.y * curr_delta_px.x;

						const mat3 mtx_view_inv = mat3(glm::inverse(scene_view.mtx_V));

						const vec3 cam_forward =
							glm::normalize(vec3(mtx_view_inv[2][0], mtx_view_inv[2][1], mtx_view_inv[2][2]));

						const float view_dot_axis_sign = glm::sign(glm::dot(axis_dir_unit, cam_forward));
						angle_delta = view_dot_axis_sign * std::atan2(sin_angle_delta, cos_angle_delta);
					}

					if (m_snapping) {
						angle_delta = std::round(angle_delta / snap_step) * snap_step;
					}

					const quat delta_rot_quat = glm::angleAxis(angle_delta, axis_dir_unit);
					const quat new_rotation   = glm::normalize(delta_rot_quat * m_drag_start_rot);

					SetTransform transform_cmd {
						.entity             = m_entity,
						.transform.position = m_drag_start_pos,
						.transform.rotation = new_rotation,
						.transform.scale    = m_scale
					};

					m_cmd_stream->push(CmdType::SetTransform, transform_cmd);
					m_rotation = new_rotation;
					return true;
				};

				switch (m_active_axis) {
				case edt::GizmoAxis::X: {
					const vec3 x_axis_dir_unit = glm::normalize(axis_x);
					return apply_axis_rotation(x_axis_dir_unit);
				}
				break;

				case edt::GizmoAxis::Y: {
					const vec3 y_axis_dir_unit = glm::normalize(axis_y);
					return apply_axis_rotation(y_axis_dir_unit);
				}
				break;

				case edt::GizmoAxis::Z: {
					const vec3 z_axis_dir_unit = glm::normalize(axis_z);
					return apply_axis_rotation(z_axis_dir_unit);
				}
				break;

				default: break;
				}
			}

			if (m_mode == edt::GizmoMode::Scale) {
				vec3 new_scale = m_drag_start_scale;

				constexpr float min_scale       = 0.001f;
				constexpr float scale_snap_step = 0.1f;

				auto commit_scale =
				[this](const vec3& new_scale) -> bool
				{
					SetTransform transform_cmd {
						.entity             = m_entity,
						.transform.position = m_drag_start_pos,
						.transform.rotation = m_rotation,
						.transform.scale    = new_scale
					};

					m_cmd_stream->push(CmdType::SetTransform, transform_cmd);
					m_scale = new_scale;
					return true;
				};

				auto apply_axis_scale =
				[
					this,
					&axis_screen,
					&project_pixels_axis,
					&drag_delta_px,
					&new_scale,
					axis_len_px,
					min_scale,
					scale_snap_step,
					&commit_scale
				](
					const vec3& axis_dir,
					float drag_start_scale_component,
					float& scale_component
				) -> bool
				{
					const vec2 axis_screen_vec = axis_screen(m_drag_start_pos, axis_dir);

					float scale_factor = 1.0f + project_pixels_axis(drag_delta_px, axis_screen_vec) / axis_len_px;
					if (m_snapping) {
						scale_factor = std::round(scale_factor / scale_snap_step) * scale_snap_step;
					}

					scale_component = glm::max(min_scale, drag_start_scale_component * scale_factor);
					return commit_scale(new_scale);
				};

				auto apply_plane_scale =
				[
					this,
					&axis_screen,
					&pixels_to_axis_components,
					&drag_delta_px,
					&new_scale,
					axis_len_px,
					min_scale,
					scale_snap_step,
					&commit_scale
				](
					const vec3& axis_dir_u,
					const vec3& axis_dir_v,
					float       drag_start_scale_u,
					float       drag_start_scale_v,
					float&      scale_u,
					float&      scale_v
				) -> bool
				{
					const vec2 axis_u_screen_vec = axis_screen(m_drag_start_pos, axis_dir_u);
					const vec2 axis_v_screen_vec = axis_screen(m_drag_start_pos, axis_dir_v);

					const vec2 axis_components_px =
						pixels_to_axis_components(drag_delta_px, axis_u_screen_vec, axis_v_screen_vec);

					float scale_factor_u = 1.0f + axis_components_px.x / axis_len_px;
					float scale_factor_v = 1.0f + axis_components_px.y / axis_len_px;

					if (m_snapping) {
						scale_factor_u = std::round(scale_factor_u / scale_snap_step) * scale_snap_step;
						scale_factor_v = std::round(scale_factor_v / scale_snap_step) * scale_snap_step;
					}

					scale_u = glm::max(min_scale, drag_start_scale_u * scale_factor_u);
					scale_v = glm::max(min_scale, drag_start_scale_v * scale_factor_v);

					return commit_scale(new_scale);
				};

				switch (m_active_axis) {
				case edt::GizmoAxis::X: {
					return apply_axis_scale(
						axis_x,
						m_drag_start_scale.x,
						new_scale.x
					);
				}
				break;

				case edt::GizmoAxis::Y: {
					return apply_axis_scale(
						axis_y,
						m_drag_start_scale.y,
						new_scale.y
					);
				}
				break;

				case edt::GizmoAxis::Z: {
					return apply_axis_scale(
						axis_z,
						m_drag_start_scale.z,
						new_scale.z
					);
				}
				break;

				case edt::GizmoAxis::XY: {
					return apply_plane_scale(
						axis_x,
						axis_y,
						m_drag_start_scale.x,
						m_drag_start_scale.y,
						new_scale.x,
						new_scale.y
					);
				}
				break;

				case edt::GizmoAxis::XZ: {
					return apply_plane_scale(
						axis_x,
						axis_z,
						m_drag_start_scale.x,
						m_drag_start_scale.z,
						new_scale.x,
						new_scale.z
					);
				}
				break;

				case edt::GizmoAxis::YZ: {
					return apply_plane_scale(
						axis_y,
						axis_z,
						m_drag_start_scale.y,
						m_drag_start_scale.z,
						new_scale.y,
						new_scale.z
					);
				}
				break;
				}
			}

			return false;
		}
		break;

		case ActionKind::GizmoEnd:
		{
			m_active      = false;
			m_active_axis = edt::GizmoAxis::None;
			m_snapping    = false;

			m_drag_accum_px = {0.0f, 0.0f};
		}
		break;

		case ActionKind::SnapOn:
		{
			m_snapping = true;
		}
		break;

		case ActionKind::SnapOff:
		{
			m_snapping = false;
		}
		break;

		case ActionKind::GizmoSetTranslate:
		{
			if (!m_active)
				m_mode = edt::GizmoMode::Translate;
		}
		break;

		case ActionKind::GizmoSetRotate:
		{
			if (!m_active)
				m_mode = edt::GizmoMode::Rotate;
		}
		break;

		case ActionKind::GizmoSetScale:
		{
			if (!m_active)
				m_mode = edt::GizmoMode::Scale;
		}
		break;
		}
	}

	return false;
}


void GizmoLayer::on_update(float delta_time)
{
	(void) delta_time;

	m_screen_scale = m_renderer.world_size_per_pixel(m_position);
}


void GizmoLayer::on_submit(rdr::Renderer& renderer, uint32_t layer_index)
{
	if (m_entity == ecs::invalid_entity)
		return;

	const auto& gizmo_style = m_gizmo_primitives.style;

	const float axis_len    = gizmo_style.axis_len_px    * m_screen_scale;
	const float plane_side  = gizmo_style.plane_side_px  * m_screen_scale;
	const float ring_radius = gizmo_style.ring_radius_px * m_screen_scale;
	const float tip_cube    = gizmo_style.tip_cube_px    * m_screen_scale;

	const mat4 base_rot = (m_space == edt::TransformSpace::Local) ? glm::mat4_cast(m_rotation) : mat4(1.0f);
	const mat4 base_tr  = glm::translate(mat4(1.0f), m_position);
	const mat4 base     = base_tr * base_rot;

	const vec4 col_plane = vec4(1.0f, 1.0f, 1.0f, gizmo_style.alpha_plane);

	const vec4 col_x = vec4(1.000f, 0.280f, 0.280f, gizmo_style.alpha_axis);
	const vec4 col_y = vec4(0.320f, 0.980f, 0.320f, gizmo_style.alpha_axis);
	const vec4 col_z = vec4(0.320f, 0.540f, 1.000f, gizmo_style.alpha_axis);

	auto push = [this, layer_index](uint32_t submesh, const mat4& model, const vec4& color)
	{
		rdr::OverlayDrawCommand overlay_cmd {
			.mesh        = m_gizmo_primitives.mesh,
			.submesh_idx = submesh,
			.sort_key    = (layer_index << 24),
			.layer_index = layer_index,
			.mtx_m       = model,
			.rgba        = color
		};

		m_renderer.overlay_queue().push(overlay_cmd);
	};

	const mat4 scale_axis  = glm::scale(glm::mat4(1.0f), vec3(axis_len, axis_len, axis_len));
	const mat4 scale_plane = glm::scale(glm::mat4(1.0f), vec3(plane_side, plane_side, plane_side));
	const mat4 scale_ring  = glm::scale(glm::mat4(1.0f), vec3(ring_radius, ring_radius, ring_radius));
	const mat4 scale_tip   = glm::scale(glm::mat4(1.0f), vec3(tip_cube, tip_cube, tip_cube));

	const mat4 translate_tip = glm::translate(glm::mat4(1.0f), vec3(0.0f, 0.0f, axis_len));

	const mat4 rot_x = glm::rotate(glm::mat4(1.0f), -glm::half_pi<float>(), vec3(0.0f, 1.0f, 0.0f));
	const mat4 rot_y = glm::rotate(glm::mat4(1.0f), -glm::half_pi<float>(), vec3(1.0f, 0.0f, 0.0f));
	const mat4 rot_z = glm::mat4(1.0f);

	const mat4 rot_xy = glm::mat4(1.0f);
	const mat4 rot_xz = glm::rotate(glm::mat4(1.0f),  glm::half_pi<float>(), vec3(1.0f, 0.0f, 0.0f));
	const mat4 rot_yz = glm::rotate(glm::mat4(1.0f), -glm::half_pi<float>(), vec3(0.0f, 1.0f, 0.0f));

	switch (m_mode) {
	case edt::GizmoMode::Translate: {

		push(m_gizmo_primitives.submesh_arrow, base * rot_x * scale_axis, col_x);
		push(m_gizmo_primitives.submesh_arrow, base * rot_y * scale_axis, col_y);
		push(m_gizmo_primitives.submesh_arrow, base * rot_z * scale_axis, col_z);

		push(m_gizmo_primitives.submesh_quad, base * rot_xy * scale_plane, col_plane);
		push(m_gizmo_primitives.submesh_quad, base * rot_xz * scale_plane, col_plane);
		push(m_gizmo_primitives.submesh_quad, base * rot_yz * scale_plane, col_plane);
	}
	break;

	case edt::GizmoMode::Rotate: {

		push(m_gizmo_primitives.submesh_ring, base * rot_xy * scale_ring, col_x);
		push(m_gizmo_primitives.submesh_ring, base * rot_xz * scale_ring, col_y);
		push(m_gizmo_primitives.submesh_ring, base * rot_yz * scale_ring, col_z);
	}
	break;

	case edt::GizmoMode::Scale: {

		push(m_gizmo_primitives.submesh_cube, base * rot_x * translate_tip * scale_tip, col_x);
		push(m_gizmo_primitives.submesh_cube, base * rot_y * translate_tip * scale_tip, col_y);
		push(m_gizmo_primitives.submesh_cube, base * rot_z * translate_tip * scale_tip, col_z);
	}
	break;

	case edt::GizmoMode::None:
	break;
	}
}


void GizmoLayer::on_result(Event& event)
{
	(void) event;
}

} // hpr

