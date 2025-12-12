#pragma once

#include "math.hpp"

#include "camera.hpp"
#include "entity.hpp"
#include "ecs_registry.hpp"
#include "draw_view_data.hpp"
#include "components_scene.hpp"


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
	static void update(ecs::Registry<Components...>& registry, ecs::Entity selected_entity, const ecs::TranslateInput& input)
	{
		if (selected_entity == ecs::invalid_entity || !registry.alive(selected_entity))
			return;

		ecs::TransformComponent* transform = registry.template get<ecs::TransformComponent>(selected_entity);
		if (!transform)
			return;

		vec3 parent_space_delta = input.delta;

		const ecs::HierarchyComponent* hierarchy = registry.template get<ecs::HierarchyComponent>(selected_entity);
		if (hierarchy && hierarchy->parent != ecs::invalid_entity) {
			const ecs::TransformComponent* parent_transform = registry.template get<ecs::TransformComponent>(hierarchy->parent);

			if (parent_transform) {
				const mat3 parent_linear     = mat3(parent_transform->world);
				const mat3 parent_linear_inv = glm::inverse(parent_linear);
				const vec3 converted_delta   = parent_linear_inv * input.delta;

				parent_space_delta = converted_delta;
			}
		}

		transform->position += parent_space_delta;

		if (input.snap_step > 0.0f) {

			mat4 parent_world = mat4(1.0f);

			if (hierarchy && hierarchy->parent != ecs::invalid_entity) {
				const ecs::TransformComponent* parent_transform = registry.template get<ecs::TransformComponent>(hierarchy->parent);
				if (parent_transform) {
					parent_world = parent_transform->world;
				}
			}

			const mat4 local_matrix = glm::translate(mat4(1.0f), transform->position) * glm::mat4_cast(transform->rotation) * glm::scale(mat4(1.0f), transform->scale);
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

			mat4 local_matrix = glm::translate(mat4(1.0f), transform->position) * glm::mat4_cast(transform->rotation) * glm::scale(mat4(1.0f), transform->scale);
			transform->world  = parent_world * local_matrix;

			const HierarchyComponent* hierarchy = registry.template get<HierarchyComponent>(entity);
			if (!hierarchy)
				return;

			Entity child_entity = hierarchy->first_child;

			while (child_entity != invalid_entity) {
				self(self, child_entity, transform->world);
				const HierarchyComponent* child_hierarchy = registry.template get<HierarchyComponent>(child_entity);
				child_entity = child_hierarchy ? child_hierarchy->next_sibling : invalid_entity;
			}
		};

		registry.template each<TransformComponent>([&registry, &visit](Entity entity, TransformComponent&)
		{
			const HierarchyComponent* hierarchy = registry.template get<HierarchyComponent>(entity);

			if (hierarchy && hierarchy->parent != invalid_entity)
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
		ecs::Entity active_cam = ecs::invalid_entity;

		registry.template each<CameraComponent>(
			[&active_cam](ecs::Entity entity, CameraComponent& camera_component)
			{
				if (active_cam != ecs::invalid_entity)
					return;

				if (camera_component.active)
					active_cam = entity;
			}
		);

		return active_cam;
	}

	template <typename... Components>
	static bool init_camera_controller(Registry<Components...>& registry, ecs::Entity active_cam_entity, scn::Camera& cam_controller)
	{
		if (active_cam_entity == ecs::invalid_entity)
			return false;

		const TransformComponent* transform_component = registry.template get<TransformComponent>(active_cam_entity);
		if (!transform_component)
			return false;

		cam_controller.position = vec3(transform_component->world[3]);

		vec3 forward_world = -vec3(transform_component->world[2]);
		const float len = glm::length(forward_world);
		if (len > 0.0f) {
			forward_world = glm::normalize(forward_world);
		}

		cam_controller.yaw = std::atan2(forward_world.x, -forward_world.z);

		forward_world.y = glm::clamp(forward_world.y, -1.0f, 1.0f);
		cam_controller.pitch = std::asin(forward_world.y);

		return true;
	}

	static mat4 make_view_matrix(const scn::Camera& cam_controller)
	{
		const float sin_y = std::sin(cam_controller.yaw);
		const float cos_y = std::cos(cam_controller.yaw);
		const float sin_p = std::sin(cam_controller.pitch);
		const float cos_p = std::cos(cam_controller.pitch);

		const vec3 forward_dir {sin_y * cos_p, sin_p, -cos_y * cos_p};
		const vec3 center = cam_controller.position + glm::normalize(forward_dir);
		const vec3 up {0.0f, 1.0f, 0.0f};

		return glm::lookAt(cam_controller.position, center, up);
	}

	template <typename... Components>
	static void apply_camera_controller(Registry<Components...>& registry, ecs::Entity active_cam_entity, const scn::Camera& cam_controller)
	{
		if (active_cam_entity == ecs::invalid_entity)
			return;

		TransformComponent* transform_component = registry.template get<TransformComponent>(active_cam_entity);
		if (!transform_component)
			return;

		const mat4 view_mtx  = make_view_matrix(cam_controller);
		const mat4 world_mtx = glm::inverse(view_mtx);

		const quat world_rotation = glm::quat_cast(world_mtx);

		transform_component->position = cam_controller.position;
		transform_component->rotation = world_rotation;
		transform_component->scale    = vec3(1.0f);
	}

	template <typename... Components>
	static bool build_view(Registry<Components...>& registry, ecs::Entity active_cam_entity, float aspect, rdr::DrawView& draw_view)
	{
		if (active_cam_entity == ecs::invalid_entity)
			return false;

		const auto* transform_component = registry.template get<TransformComponent>(active_cam_entity);
		const auto* camera_component    = registry.template get<CameraComponent>(active_cam_entity);

		if (!transform_component || !camera_component)
			return false;

		mat4 mtx_V = glm::inverse(transform_component->world);
		mat4 mtx_P = glm::perspective(glm::radians(camera_component->fov_deg), aspect, camera_component->znear, camera_component->zfar);
		mat4 mtx_VP = mtx_P * mtx_V;

		draw_view.mtx_V  = mtx_V;
		draw_view.mtx_P  = mtx_P;
		draw_view.mtx_VP = mtx_VP;

		draw_view.pos_world =  vec3(transform_component->world[3]);
		draw_view.fwd_world = -vec3(transform_component->world[2]);

		draw_view.near = camera_component->znear;
		draw_view.far  = camera_component->zfar;

		draw_view.frustum = math::frustum_planes(mtx_VP);

		return true;
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

				if (hierarchy && hierarchy->parent != ecs::invalid_entity) {
					const ecs::TransformComponent* parent =
						registry.template get<ecs::TransformComponent>(hierarchy->parent);

					if (parent)
						transform.world = parent->world * local_mtx;
					else
						transform.world = local_mtx;
				}
				else {
					transform.world = local_mtx;
				}
			}
		);
	}
};


struct LightSystem
{
	template <typename... Components>
	static void build_light(ecs::Registry<Components...>& registry, const rdr::DrawView& draw_view, rdr::DrawViewLightSet& draw_view_light_set)
	{
		static thread_local mtp::slag<rdr::DrawViewLight, mtp::default_set> draw_view_light_set_buffer {64};

		draw_view_light_set_buffer.clear();
		const mat4& mtx_V = draw_view.mtx_V;

		registry.template scan<LightComponent, TransformComponent>(
			[&mtx_V](Entity, LightComponent& light_component, const TransformComponent& transform_component)
			{
				if (!light_component.enabled)
					return;
				if (draw_view_light_set_buffer.size() >= scn::k_max_light_count)
					return;

				rdr::DrawViewLight view_light {};
				view_light.type = static_cast<uint8_t>(light_component.type);
				view_light.color_rgb = light_component.color_rgb;
				view_light.intensity = light_component.intensity;

				const vec4 dir_world = vec4(vec3(transform_component.world[2]), 0.0f);
				const vec4 pos_world = vec4(vec3(transform_component.world[3]), 1.0f);

				const vec4 dir_view = mtx_V * dir_world;
				const vec4 pos_view = mtx_V * pos_world;

				view_light.dir_view = vec3(dir_view);
				view_light.pos_view = vec3(pos_view);

				view_light.range = light_component.range;
				view_light.cos_inner = std::cos(glm::radians(light_component.inner_deg));
				view_light.cos_outer = std::cos(glm::radians(light_component.outer_deg));

				draw_view_light_set_buffer.emplace_back(view_light);
			});

		draw_view_light_set.items = draw_view_light_set_buffer.empty() ? nullptr : &draw_view_light_set_buffer[0];
		draw_view_light_set.count = static_cast<uint32_t>(draw_view_light_set_buffer.size());
	}
};


} // hpr::ecs

