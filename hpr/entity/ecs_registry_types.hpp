#pragma once

#include "ecs_registry.hpp"

#include "components_scene.hpp"
#include "components_render.hpp"


namespace hpr {


using MainRegistry = ecs::Registry <
	ecs::TransformComponent,
	ecs::HierarchyComponent,
	ecs::NameComponent,
	ecs::ModelComponent,
	ecs::AnimComponent,
	ecs::OccluderComponent,
	ecs::CameraComponent,
	ecs::LightComponent
>;

} // hpr
