#pragma once

#include "math.hpp"
#include "ecs_registry.hpp"
#include "components_scene.hpp"
#include "components_render.hpp"


namespace hpr::ecs {


struct BoundSystem
{
	template <typename... Components>
	static void update(Registry<Components...>& registry)
	{
		registry.template scan<TransformComponent, BoundComponent>(
			[](Entity, const TransformComponent& transform, BoundComponent& bound)
			{
				const mat3 linear_world = mat3(transform.world);

				const mat3 abs_linear {
					glm::abs(linear_world[0]),
					glm::abs(linear_world[1]),
					glm::abs(linear_world[2])
				};

				const vec4 center_local_hom(bound.local_center, 1.0f);
				const vec4 center_world_hom = transform.world * center_local_hom;

				bound.world_center = vec3(center_world_hom);
				bound.world_half   = abs_linear * bound.local_half;
			}
		);
	}
};


} // hpr::ecs

