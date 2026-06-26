#pragma once

#include "math.hpp"

#include "entity.hpp"
#include "ecs_registry.hpp"
#include "draw_view_data.hpp"
#include "components_scene.hpp"
#include "camera_controller.hpp"
#include "panic.hpp"


namespace hpr::ecs {


struct TranslateInput
{
	vec3  delta;
	float snap_step;

	enum class Space {World, Local} space;
};


struct TranslateSystem
{
	static inline float snap_value(float value, float step)
	{
		if (step <= 0.0f)
			return value;

		return std::round(value / step) * step;
	}

	template <typename... Components>
	static void update(
		ecs::Registry<Components...>& registry,
		ecs::Entity                   selected_entity,
		const ecs::TranslateInput&    input
	)
	{
		if (selected_entity == ctx::invalid_entity || !registry.alive(selected_entity))
			return;

		ecs::TransformComponent* transform = registry.template get<ecs::TransformComponent>(selected_entity);
		if (!transform)
			return;

		vec3 parent_space_delta = input.delta;

		const ecs::HierarchyComponent* hierarchy = registry.template get<ecs::HierarchyComponent>(selected_entity);
		if (hierarchy && hierarchy->parent != ctx::invalid_entity) {

			const ecs::TransformComponent* parent_transform =
				registry.template get<ecs::TransformComponent>(hierarchy->parent);

			if (parent_transform) {
				const mat3 parent_linear     = mat3(parent_transform->mtx_W);
				const mat3 parent_linear_inv = glm::inverse(parent_linear);
				const vec3 converted_delta   = parent_linear_inv * input.delta;

				parent_space_delta = converted_delta;
			}
		}

		transform->position += parent_space_delta;

		if (input.snap_step > 0.0f) {

			mat4 parent_world = mat4(1.0f);

			if (hierarchy && hierarchy->parent != ctx::invalid_entity) {
				const ecs::TransformComponent* parent_transform =
					registry.template get<ecs::TransformComponent>(hierarchy->parent);

				if (parent_transform) {
					parent_world = parent_transform->mtx_W;
				}
			}

			const mat4 local_matrix =
				glm::translate(mat4(1.0f), transform->position) *
				glm::mat4_cast(transform->rotation)             *
				glm::scale(mat4(1.0f), transform->scale);

			const mat4 world_matrix = parent_world * local_matrix;
			const vec4 origin_local = vec4(0.0f, 0.0f, 0.0f, 1.0f);

			vec4 world_pos = world_matrix * origin_local;

			const vec3 world_pos_snapped = glm::round(vec3(world_pos) / input.snap_step) * input.snap_step;
			world_pos = vec4(world_pos_snapped, world_pos.w);

			const mat4 inv_parent = glm::inverse(parent_world);
			const vec4 local_pos  = inv_parent * world_pos;

			transform->position = vec3(local_pos);
		}
	}
};


struct HierarchySystem
{
	template <typename... Components>
	static void update(Registry<Components...>& registry)
	{
		auto visit = [&registry](auto&& self, Entity entity, const mat4 parent_world) -> void
		{
			TransformComponent* transform = registry.template get<TransformComponent>(entity);
			if (!transform)
				return;

			mat4 local_matrix =
				glm::translate(mat4(1.0f), transform->position) *
				glm::mat4_cast(transform->rotation)             *
				glm::scale(mat4(1.0f), transform->scale);

			transform->mtx_W = parent_world * local_matrix;

			const HierarchyComponent* hierarchy = registry.template get<HierarchyComponent>(entity);
			if (!hierarchy)
				return;

			Entity child_entity = hierarchy->first_child;

			while (child_entity != ctx::invalid_entity) {
				self(self, child_entity, transform->mtx_W);
				const HierarchyComponent* child_hierarchy = registry.template get<HierarchyComponent>(child_entity);
				child_entity = child_hierarchy ? child_hierarchy->next_sibling : ctx::invalid_entity;
			}
		};

		registry.template each<TransformComponent>([&registry, &visit](Entity entity, TransformComponent&)
		{
			const HierarchyComponent* hierarchy = registry.template get<HierarchyComponent>(entity);

			if (hierarchy && hierarchy->parent != ctx::invalid_entity)
				return;

			mat4 identity_parent = mat4(1.0f);

			visit(visit, entity, identity_parent);
		});
	}
};


struct CameraSystem
{
	template <typename... Components>
	static ecs::Entity find_active_camera(Registry<Components...>& registry)
	{
		ecs::Entity active_cam = ctx::invalid_entity;

		registry.template each<CameraComponent>(
			[&active_cam](ecs::Entity entity, CameraComponent& camera_component)
			{
				if (active_cam != ctx::invalid_entity)
					return;

				if (camera_component.active)
					active_cam = entity;
			}
		);

		return active_cam;
	}

	template <typename... Components>
	static bool init_camera_controller(
		Registry<Components...>& registry,
		ecs::Entity              active_cam_entity,
		scn::CameraController&   cam_controller
	)
	{
		if (active_cam_entity == ctx::invalid_entity)
			return false;

		if (cam_controller.mode == scn::CameraController::Mode::iso) {
			cam_controller.yaw   = cam_controller.iso_yaw;
			cam_controller.pitch = cam_controller.iso_pitch;
			cam_controller.delta = {};
			return true;
		}

		const TransformComponent* transform_component =
			registry.template get<TransformComponent>(active_cam_entity);

		if (!transform_component)
			return false;

		vec3 forward_world = transform_component->world_fwd();

		const float len = glm::length(forward_world);
		if (len > 0.0f) {
			forward_world = glm::normalize(forward_world);
		}
		else {
			forward_world = vec3(0.0f, 0.0f, -1.0f);
		}

		cam_controller.yaw = std::atan2(forward_world.x, -forward_world.z);

		forward_world.y = glm::clamp(forward_world.y, -1.0f, 1.0f);
		cam_controller.pitch = std::asin(forward_world.y);

		cam_controller.delta = {};

		return true;
	}


	template <typename... Components>
	static rdr::DrawView build_view(
		Registry<Components...>&     registry,
		ecs::Entity                  active_cam,
		const scn::CameraController& cam_controller,
		float                        aspect
	)
	{
		rdr::DrawView draw_view {};

		HPR_ASSERT(active_cam != ctx::invalid_entity);

		const auto* transform_component =
			registry.template get<TransformComponent>(active_cam);
		const auto* camera_component =
			registry.template get<CameraComponent>(active_cam);

		HPR_ASSERT(transform_component && camera_component);

		mat4 mtx_V = glm::inverse(transform_component->mtx_W);
		mat4 mtx_P {};

		float znear, zfar;

		if (cam_controller.mode == scn::CameraController::Mode::iso) {

			znear = -cam_controller.iso_min_depth_span;
			zfar  =  cam_controller.iso_min_depth_span;

			const float half_h = 0.5f   * cam_controller.iso_ortho_height;
			const float half_w = half_h * aspect;

			mtx_P = glm::ortho(
				-half_w,
				half_w,
				-half_h,
				half_h,
				znear,
				zfar
			);
		}
		else {

			znear = std::max(camera_component->znear, 0.1f);
			zfar  = std::max(camera_component->zfar, znear + 0.1f);

			mtx_P = glm::perspective(
				glm::radians(camera_component->fov_deg),
				aspect,
				znear,
				zfar
			);
		}

		mat4 mtx_VP = mtx_P * mtx_V;

		draw_view.mtx_V  = mtx_V;
		draw_view.mtx_P  = mtx_P;
		draw_view.mtx_VP = mtx_VP;

		draw_view.pos_W = transform_component->position;
		draw_view.fwd_W = transform_component->world_fwd();

		draw_view.near = znear;
		draw_view.far  = zfar;

		draw_view.frustum = math::frustum_planes(mtx_VP);

		return draw_view;
	}


	template <typename... Components>
	static void update_camera_controller(
		Registry<Components...>& registry,
		ecs::Entity              active_cam_entity,
		scn::CameraController&   cam_controller,
		float                    delta_time,
		float                    pan_sensitivity,
		float                    dolly_sensitivity
	)
	{
		if (active_cam_entity == ctx::invalid_entity)
			return;

		TransformComponent* transform_component =
			registry.template get<TransformComponent>(active_cam_entity);

		if (!transform_component)
			return;

		const bool iso_cam = cam_controller.mode == scn::CameraController::Mode::iso;

		if (!iso_cam &&
			(cam_controller.delta.orbit_x != 0.0f || cam_controller.delta.orbit_y != 0.0f)) {
				cam_controller.look_delta(cam_controller.delta.orbit_x, cam_controller.delta.orbit_y);
		}

		const float yaw   = iso_cam ? cam_controller.iso_yaw   : cam_controller.yaw;
		const float pitch = iso_cam ? cam_controller.iso_pitch : cam_controller.pitch;

		const quat qt_yaw     = glm::angleAxis(yaw, vec3(0.0f, 1.0f, 0.0f));
		const vec3 right_axis = qt_yaw * vec3(1.0f, 0.0f, 0.0f);
		const quat qt_pitch   = glm::angleAxis(pitch, right_axis);

		const quat rot = qt_pitch * qt_yaw;

		const vec3 fwd_vec   = rot * vec3(0.0f, 0.0f, -1.0f);
		const vec3 right_vec = rot * vec3(1.0f, 0.0f,  0.0f);

		vec3 move_fwd_vec   = fwd_vec;
		vec3 move_right_vec = right_vec;
		
		if (iso_cam) {

			move_fwd_vec.y   = 0.0f;
			move_right_vec.y = 0.0f;
		
			const float fwd_len2   = glm::dot(move_fwd_vec,   move_fwd_vec);
			const float right_len2 = glm::dot(move_right_vec, move_right_vec);
		
			if (fwd_len2 > 0.0f)   move_fwd_vec   *= glm::inversesqrt(fwd_len2);
			if (right_len2 > 0.0f) move_right_vec *= glm::inversesqrt(right_len2);
		}

		const vec3 world_up {0.0f, 1.0f, 0.0f};

		const float pan_x = pan_sensitivity * cam_controller.delta.pan_x;
		const float pan_y = pan_sensitivity * cam_controller.delta.pan_y;

		if (pan_x != 0.0f || pan_y != 0.0f) {
			transform_component->position += right_vec * pan_x;
			transform_component->position += world_up  * pan_y;
		}

		const float dolly = dolly_sensitivity * cam_controller.delta.dolly;

		if (dolly != 0.0f) {
			if (iso_cam) {
				cam_controller.iso_ortho_height *= (1.0f - dolly);
				if (cam_controller.iso_ortho_height < 0.01f) {
					cam_controller.iso_ortho_height = 0.01f;
				}
			}
			else {
				transform_component->position += fwd_vec * dolly;
			}
		}

		if (
			cam_controller.delta.move_forward != 0.0f ||
			cam_controller.delta.move_right   != 0.0f ||
			cam_controller.delta.move_up      != 0.0f
		) {
			const float move_step = cam_controller.move_speed * delta_time;

			transform_component->position += move_fwd_vec   * (move_step * cam_controller.delta.move_forward);
			transform_component->position += move_right_vec * (move_step * cam_controller.delta.move_right);
			transform_component->position += world_up       * (move_step * cam_controller.delta.move_up);
		}

		transform_component->rotation = rot;
		transform_component->scale    = vec3(1.0f);

		cam_controller.delta = {};
	}

};


struct TransformSystem
{
	template <typename... Components>
	static void update(ecs::Registry<Components...>& registry)
	{
		registry.template scan<ecs::TransformComponent>(
			[&registry](ecs::Entity entity, ecs::TransformComponent& transform)
			{
				const mat4 local_mtx = glm::translate(mat4(1.0f), transform.position) * glm::mat4_cast(transform.rotation) * glm::scale(mat4(1.0f), transform.scale);

				const ecs::HierarchyComponent* hierarchy =
					registry.template get<ecs::HierarchyComponent>(entity);

				if (hierarchy && hierarchy->parent != ctx::invalid_entity) {
					const ecs::TransformComponent* parent =
						registry.template get<ecs::TransformComponent>(hierarchy->parent);

					if (parent)
						transform.mtx_W = parent->mtx_W * local_mtx;
					else
						transform.mtx_W = local_mtx;
				}
				else {
					transform.mtx_W = local_mtx;
				}
			}
		);
	}
};


struct LightSystem
{
	template <typename... Components>
	static rdr::LightSet build_light(ecs::Registry<Components...>& registry, const rdr::DrawView& draw_view)
	{
		static thread_local mtp::slag<rdr::LightItem, mtp::default_set> draw_view_light_set_buffer {64};

		draw_view_light_set_buffer.clear();
		const mat4& mtx_V = draw_view.mtx_V;

		registry.template scan<LightComponent, TransformComponent>(
			[&mtx_V](
				Entity,
				LightComponent&           light_comp,
				const TransformComponent& transform_comp
			)
			{
				if (!light_comp.enabled)
					return;
				if (draw_view_light_set_buffer.size() >= scn::cfg::max_light_count)
					return;

				rdr::LightItem light {};
				light.type = static_cast<uint8_t>(light_comp.type);
				light.color_rgb = light_comp.color_rgb;
				light.intensity = light_comp.intensity;

				const vec4 dir_world = vec4(transform_comp.world_fwd(), 0.0f);
				const vec4 pos_world = vec4(transform_comp.position,    1.0f);

				light.dir_world = dir_world;
				light.pos_world = pos_world;

				const vec4 dir_view = mtx_V * dir_world;
				const vec4 pos_view = mtx_V * pos_world;

				light.dir_view = vec3(dir_view);
				light.pos_view = vec3(pos_view);

				light.range = light_comp.range;
				light.cos_inner = std::cos(glm::radians(light_comp.inner_deg));
				light.cos_outer = std::cos(glm::radians(light_comp.outer_deg));

				draw_view_light_set_buffer.emplace_back(light);
			});

		rdr::LightSet draw_view_light_set {};
		draw_view_light_set.items =
			draw_view_light_set_buffer.empty() ? nullptr : &draw_view_light_set_buffer[0];
		draw_view_light_set.count =
			static_cast<uint32_t>(draw_view_light_set_buffer.size());

		return draw_view_light_set;
	}
};


} // hpr::ecs

