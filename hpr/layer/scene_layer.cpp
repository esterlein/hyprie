#include <cmath>
#include <limits>

#include "draw_view_data.hpp"
#include "inspector_state.hpp"
#include "math.hpp"
#include "event.hpp"
#include "scene_io.hpp"
#include "scene_core.hpp"
#include "scene_layer.hpp"
#include "scene_query.hpp"
#include "systems_scene.hpp"
#include "systems_render.hpp"
#include "draw_queue_data.hpp"


namespace hpr {


namespace cfg {

	inline constexpr uint32_t job_grain = 64U;

} // hpr::cfg


struct FrustumPlane
{
	vec3  normal;
	vec3  abs_normal;
	float w;
};


struct ModelDrawInstance
{
	ecs::Entity entity;

	const ecs::ModelComponent* model;

	mat4 mtx_world;
	vec3 aabb_center;
	vec3 aabb_half;

	float world_units_per_px;
};


struct ModelDrawCmdJobSlice
{
	uint32_t begin;
	uint32_t end;

	const ModelDrawInstance* instances;
	uint32_t                 instance_count;

	const mtp::vault<scn::ScenePrimitive, mtp::default_set>* scene_primitives;

	const FrustumPlane* frustum_planes;
	uint32_t            frustum_plane_count;

	uint32_t layer_index;

	ecs::Entity selected_entity;

	mtp::slag<rdr::SceneDrawCommand, mtp::default_set>* draw_cmds;
};


SceneLayer::SceneLayer(
	ECSRegistry&            ecs_registry,
	res::AssetKeeper&       asset_keeper,
	rdr::RenderForge&       render_forge,
	rdr::Renderer&          renderer,
	const io::InputBinding& input_binding,
	scn::SceneResolver&     resolver,
	const char*             scene_path
)
	: m_registry     {ecs_registry}
	, m_asset_keeper {asset_keeper}
	, m_render_forge {render_forge}
	, m_renderer     {renderer}
	, m_binding      {input_binding}
	, m_resolver     {resolver}
	, m_scene_path   {scene_path}
{
	uint32_t worker_thread_count = std::thread::hardware_concurrency();

	if (worker_thread_count == 0) {
		worker_thread_count = 1;
	}
	else if (worker_thread_count > 1) {
		worker_thread_count -= 1;
	}

	if (worker_thread_count > 8) {
		worker_thread_count = 8;
	}


	worker_thread_count = 1;


	m_scheduler.init(worker_thread_count);
}


void SceneLayer::on_attach()
{
	scn::io::SceneDoc scene_doc {};
	if (!scn::io::read_file(m_scene_path, scene_doc)){
		// TODO: control & log
		return;
	}

	m_scene.clear();
	if (!scn::instantiate(scene_doc, m_registry, m_asset_keeper, m_render_forge, m_scene)) {
		// TODO: control & log
		return;
	}

	ecs::TransformSystem::update(m_registry);
	ecs::BoundSystem::update(m_registry);

	m_active_cam_entity = ecs::CameraSystem::find_active_camera(m_registry);
	HPR_ASSERT(m_active_cam_entity != ecs::invalid_entity);

	const bool is_cam_ok = ecs::CameraSystem::init_camera_controller(
		m_registry,
		m_active_cam_entity,
		m_camera_controller
	);
	HPR_ASSERT(is_cam_ok);
}


void SceneLayer::on_detach()
{}


bool SceneLayer::on_event(Event& event)
{
	return false;
}


bool SceneLayer::on_actions(std::span<const Action> actions)
{
	bool action_consumed = false;

	for (const Action& action : actions) {
		switch (action.kind) {
		case ActionKind::Orbit: {
			const auto& payload = std::get<OrbitAction>(action.payload);
			m_orbit_dx += payload.delta_x;
			m_orbit_dy += payload.delta_y;
		}
		break;
		case ActionKind::Pan: {
			const auto& payload = std::get<PanAction>(action.payload);
			m_pan_dx += payload.delta_x;
			m_pan_dy += payload.delta_y;
		}
		break;
		case ActionKind::Dolly: {
			const auto& payload = std::get<DollyAction>(action.payload);
			m_dolly += payload.amount;
		}
		break;
		case ActionKind::Move: {
			const auto& payload = std::get<MoveAction>(action.payload);
			m_move_forward += payload.forward;
			m_move_right   += payload.right;
			m_move_up      += payload.up;
		}
		break;

		case ActionKind::SelectClick:
		{
			const auto& payload = std::get<SelectClickAction>(action.payload);
			const auto surface_info = m_renderer.surface_info();

			scn::Ray ray = scn::make_pick_ray(
				payload.x,
				payload.y,
				surface_info.width,
				surface_info.height,
				m_draw_view
			);

			const scn::RayHit ray_hit = scn::raycast_scene(ray, m_registry, m_scene, m_resolver);


			if (ray_hit.hit) {
				m_selection.entity = ray_hit.entity;

				const auto* transform = m_registry.get<ecs::TransformComponent>(m_selection.entity);
				HPR_ASSERT(transform);

				// TODO: overload for compact assignments

				m_selection.transform.position = transform->position;
				m_selection.transform.rotation = transform->rotation;
				m_selection.transform.scale    = transform->scale;

				m_selection.submesh = ray_hit.submesh;
			}
			else {
				m_selection.entity    = ecs::invalid_entity;
				m_selection.transform = {};
				m_selection.submesh   = std::numeric_limits<uint32_t>::max();
			}

			auto* event = m_event_queue->push<SelectionChangedEvent>();

			event->selection.entity    = m_selection.entity;
			event->selection.transform = m_selection.transform;
			event->selection.submesh   = m_selection.submesh;
			event->emitter = this;

			action_consumed = true;
		}
		break;

		default: break;
		}
	}
	return action_consumed;
}


void SceneLayer::on_update(float delta_time)
{
	if (m_orbit_dx != 0.0f || m_orbit_dy != 0.0f) {
		m_camera_controller.look_delta(m_orbit_dx, m_orbit_dy);
	}

	const float sin_yaw   = std::sin(m_camera_controller.yaw);
	const float cos_yaw   = std::cos(m_camera_controller.yaw);
	const float sin_pitch = std::sin(m_camera_controller.pitch);
	const float cos_pitch = std::cos(m_camera_controller.pitch);

	vec3 forward = glm::normalize(vec3 {sin_yaw * cos_pitch, sin_pitch, -cos_yaw * cos_pitch});

	const vec3 world_up = {0.0f, 1.0f, 0.0f};
	vec3 right_vec = glm::cross(forward, world_up);

	if (glm::dot(right_vec, right_vec) < math::magnitude_sq_epsilon) {
		right_vec = {cos_yaw, 0.0f, sin_yaw};
	}
	else {
		right_vec = glm::normalize(right_vec);
	}

	if (m_pan_dx != 0.0f || m_pan_dy != 0.0f) {
		const float pan_step_x = m_binding.pan_sensitivity * m_pan_dx;
		const float pan_step_y = m_binding.pan_sensitivity * m_pan_dy;

		m_camera_controller.position += right_vec * pan_step_x;
		m_camera_controller.position += world_up  * pan_step_y;
	}

	if (m_dolly != 0.0f) {
		const float dolly_step = m_binding.dolly_sensitivity * m_dolly;

		m_camera_controller.position += forward * dolly_step;
	}

	if (m_move_forward != 0.0f || m_move_right != 0.0f || m_move_up != 0.0f) {
		const float move_step = m_camera_controller.move_speed * delta_time;

		m_camera_controller.position += forward   * (move_step * m_move_forward);
		m_camera_controller.position += right_vec * (move_step * m_move_right);
		m_camera_controller.position += world_up  * (move_step * m_move_up);
	}

	m_orbit_dx     = 0.0f;
	m_orbit_dy     = 0.0f;
	m_pan_dx       = 0.0f;
	m_pan_dy       = 0.0f;
	m_dolly        = 0.0f;
	m_move_forward = 0.0f;
	m_move_right   = 0.0f;
	m_move_up      = 0.0f;

	ecs::CameraSystem::apply_camera_controller(m_registry, m_active_cam_entity, m_camera_controller);

	ecs::HierarchySystem::update(m_registry);
	ecs::TransformSystem::update(m_registry);
	ecs::BoundSystem::update(m_registry);

	const float aspect_ratio = m_renderer.surface_info().aspect;

	ecs::CameraSystem::build_view(m_registry, m_active_cam_entity, aspect_ratio, m_draw_view);

	m_draw_view_light_set.ambient_rgb = glm::make_vec3(m_scene.ambient());

	ecs::LightSystem::build_light(m_registry, m_draw_view, m_draw_view_light_set);
}



void SceneLayer::on_submit(rdr::Renderer& renderer, uint32_t layer_index)
{
	m_renderer.set_context(FrameContext {m_draw_view, m_draw_view_light_set});

	const auto& scene_primitives = m_scene.scene_primitives();

	std::array<FrustumPlane, math::frustum_plane_count> culling_planes;

	for (size_t plane_index = 0; plane_index < culling_planes.size(); ++plane_index) {
		const vec4& plane = m_draw_view.frustum[plane_index];
		culling_planes[plane_index] = {
			vec3(plane.x, plane.y, plane.z),
			glm::abs(vec3(plane.x, plane.y, plane.z)),
			plane.w
		};
	}

	/* tiles */

	auto& chunk_drawable_set = m_scene.tile_chunk_drawable_set();

	if (chunk_drawable_set.enabled) {

		for (auto& chunk_drawable : chunk_drawable_set.drawables) {

			const int32_t storey_index = chunk_drawable.coord.storey_index;

			if (storey_index < chunk_drawable_set.storey_min || storey_index > chunk_drawable_set.storey_max) {
				continue;
			}

			const vec3 aabb_center = chunk_drawable.bounds_center;
			const vec3 aabb_half   = chunk_drawable.bounds_half;

			bool culled = false;

			for (const auto& plane : culling_planes) {

				const float aabb_proj_radius =
					plane.abs_normal.x * aabb_half.x +
					plane.abs_normal.y * aabb_half.y +
					plane.abs_normal.z * aabb_half.z;

				const float signed_distance = glm::dot(plane.normal, aabb_center) + plane.w;

				if (signed_distance < -aabb_proj_radius) {
					culled = true;
					break;
				}
			}

			if (culled) {
				continue;
			}

			if (!chunk_drawable.mesh.is_valid()) {
				chunk_drawable.mesh = m_render_forge.quad();
				chunk_drawable.submesh_idx = 0;
			}

			if (!chunk_drawable.tilemap.is_valid()) {
				chunk_drawable.tilemap = m_render_forge.create_tilemap_texture(
					scn::cfg::chunk_size,
					scn::cfg::chunk_size
				);
			}

			if (chunk_drawable.dirty) {

				auto* chunk = m_scene.tilefield().find_chunk(chunk_drawable.coord_hash);
				HPR_ASSERT_MSG(chunk,
				   "missing tile chunk for drawable hash");

				m_render_forge.update_tilemap_texture(
					chunk_drawable.tilemap,
					std::span<const uint16_t>(chunk->tiles.data(), chunk->tiles.size()),
					scn::cfg::chunk_size,
					scn::cfg::chunk_size
				);

				chunk_drawable.dirty = false;
			}

			const uint64_t sort_key =
				(static_cast<uint64_t>(layer_index) << 56) |
				(chunk_drawable.coord_hash & 0x00FFFFFFFFFFFFFFULL);

			const rdr::TileDrawCommand tile_cmd {
				.mesh        = chunk_drawable.mesh,
				.submesh_idx = chunk_drawable.submesh_idx,
				.tilemap     = chunk_drawable.tilemap,
				.tile_style  = chunk_drawable.tile_style,
				.sort_key    = sort_key,
				.layer_index = layer_index,
				.mtx_M       = chunk_drawable.mtx_M
			};

			renderer.tile_queue().push(tile_cmd);
		}
	}

	/* models */

	mtp::slag<ModelDrawInstance, mtp::default_set> model_draw_instances;

	m_registry.template scan<ecs::ModelComponent, ecs::TransformComponent, ecs::BoundComponent>(
		[&model_draw_instances, &renderer](
			ecs::Entity entity,
			ecs::ModelComponent& model,
			const ecs::TransformComponent& transform,
			const ecs::BoundComponent& aabb
		)
		{
			const float world_units_per_px =
				renderer.world_size_per_pixel(aabb.world_center);

			model_draw_instances.emplace_back(ModelDrawInstance {
				.entity             = entity,
				.model              = &model,
				.mtx_world          = transform.world,
				.aabb_center        = aabb.world_center,
				.aabb_half          = aabb.world_half,
				.world_units_per_px = world_units_per_px
			});
		}
	);

	const uint32_t model_instance_count = static_cast<uint32_t>(model_draw_instances.size());
	if (model_instance_count == 0) {
		return;
	}

	const uint32_t job_slice_count = (model_instance_count + cfg::job_grain - 1) / cfg::job_grain;

	mtp::slag<ModelDrawCmdJobSlice, mtp::default_set> draw_cmd_job_slices;
	draw_cmd_job_slices.resize(job_slice_count);

	mtp::slag<mtp::slag<rdr::SceneDrawCommand, mtp::default_set>, mtp::default_set> slice_draw_cmds;
	slice_draw_cmds.resize(job_slice_count);

	for (uint32_t job_slice_idx = 0; job_slice_idx < job_slice_count; ++job_slice_idx) {

		slice_draw_cmds[job_slice_idx].reserve(cfg::job_grain * 2);

		draw_cmd_job_slices[job_slice_idx].instances           = model_draw_instances.data();
		draw_cmd_job_slices[job_slice_idx].instance_count      = model_instance_count;
		draw_cmd_job_slices[job_slice_idx].scene_primitives    = &scene_primitives;
		draw_cmd_job_slices[job_slice_idx].frustum_planes      = culling_planes.data();
		draw_cmd_job_slices[job_slice_idx].frustum_plane_count = static_cast<uint32_t>(culling_planes.size());
		draw_cmd_job_slices[job_slice_idx].layer_index         = layer_index;
		draw_cmd_job_slices[job_slice_idx].selected_entity     = m_selection.entity;
		draw_cmd_job_slices[job_slice_idx].draw_cmds           = &slice_draw_cmds[job_slice_idx];
	}

	job::JobLatch job_latch;

	m_scheduler.dispatch_range(
		job_latch,
		&build_model_draw_cmds,
		model_instance_count,
		cfg::job_grain,
		draw_cmd_job_slices.data()
	);

	job_latch.wait();

	for (uint32_t job_slice_idx = 0; job_slice_idx < job_slice_count; ++job_slice_idx) {
		for (const auto& draw_cmd : slice_draw_cmds[job_slice_idx]) {
			renderer.scene_queue().push(draw_cmd);
		}
	}
}


void SceneLayer::on_result(Event& event)
{
	(void) event;
}


void SceneLayer::build_model_draw_cmds(void* slice_raw)
{
	auto* slice = static_cast<ModelDrawCmdJobSlice*>(slice_raw);

	auto& slice_draw_cmds = *slice->draw_cmds;
	slice_draw_cmds.clear();

	const ModelDrawInstance* model_draw_instances = slice->instances;

	for (uint32_t instance_idx = slice->begin; instance_idx < slice->end; ++instance_idx) {

		const ModelDrawInstance& model_instance = model_draw_instances[instance_idx];

		const vec3 aabb_center = model_instance.aabb_center;
		const vec3 aabb_half   = model_instance.aabb_half;

		bool is_culled = false;

		for (uint32_t plane_idx = 0; plane_idx < slice->frustum_plane_count; ++plane_idx) {

			const auto& plane = slice->frustum_planes[plane_idx];

			const float aabb_proj_radius =
				plane.abs_normal.x * aabb_half.x +
				plane.abs_normal.y * aabb_half.y +
				plane.abs_normal.z * aabb_half.z;

			const float signed_distance =
				glm::dot(plane.normal, aabb_center) + plane.w;

			if (signed_distance < -aabb_proj_radius) {
				is_culled = true;
				break;
			}
		}

		if (is_culled) {
			continue;
		}

		const float world_units_per_px =
			model_instance.world_units_per_px;

		if (world_units_per_px > 0.0f) {

			const float aabb_sphere_radius = glm::length(aabb_half);
			const float proj_diameter_px = (2.0f * aabb_sphere_radius) / world_units_per_px;

			if (proj_diameter_px < 2.0f) {
				continue;
			}
		}

		const ecs::Entity entity = model_instance.entity;
		const ecs::ModelComponent& model = *model_instance.model;
		const auto& scene_primitives = *slice->scene_primitives;

		const uint64_t sort_key =
			(static_cast<uint64_t>(slice->layer_index) << 56) |
			(static_cast<uint64_t>(entity) & 0x0000000000FFFFFFULL);

		uint8_t flags = 0;
		if (entity == slice->selected_entity) {
			flags |= static_cast<uint8_t>(rdr::SceneDrawCmdFlag::Selected);
		}

		for (uint32_t submesh_index = 0; submesh_index < model.submesh_count; ++submesh_index) {

			const auto& primitive =
				scene_primitives[model.submesh_first + submesh_index];

			const rdr::SceneDrawCommand draw_cmd {
				.mesh        = primitive.mesh,
				.submesh_idx = primitive.submesh_idx,
				.material    = primitive.material,
				.sort_key    = sort_key,
				.layer_index = slice->layer_index,
				.mtx_M       = model_instance.mtx_world,
				.flags       = flags
			};

			slice_draw_cmds.emplace_back(draw_cmd);
		}
	}
}


void SceneLayer::process_commands(CmdStream::Reader reader)
{
	CmdType type;
	const void* payload;
	uint32_t payload_size;

	while (reader.next(type, payload, payload_size))
	{
		switch (type)
		{
			case CmdType::SetTransform:
			{
				HPR_ASSERT(payload_size >= sizeof(SetTransform));

				const SetTransform* cmd = static_cast<const SetTransform*>(payload);

				if (auto* transform_comp = m_registry.get<ecs::TransformComponent>(cmd->entity)) {
					transform_comp->position = cmd->transform.position;
					transform_comp->rotation = cmd->transform.rotation;
					transform_comp->scale    = cmd->transform.scale;
				}
				break;
			}
			case CmdType::SetLight:
			{
				HPR_ASSERT(payload_size >= sizeof(SetLight));

				const SetLight* cmd = static_cast<const SetLight*>(payload);

				if (auto* light_comp = m_registry.get<ecs::LightComponent>(cmd->entity)) {
					light_comp->enabled   = cmd->light.enabled;
					light_comp->type      = static_cast<scn::LightType>(cmd->light.type);
					light_comp->color_rgb = cmd->light.color_rgb;
					light_comp->intensity = cmd->light.intensity;
					light_comp->range     = cmd->light.range;
					light_comp->inner_deg = cmd->light.inner_deg;
					light_comp->outer_deg = cmd->light.outer_deg;
				}
				break;
			}
			case CmdType::SetMaterial:
			{
				HPR_ASSERT(payload_size >= sizeof(SetMaterial));

				const SetMaterial* cmd = static_cast<const SetMaterial*>(payload);
				const auto* model_comp = m_registry.get<ecs::ModelComponent>(cmd->entity);

				if (!model_comp)
					break;

				HPR_ASSERT(cmd->submesh < model_comp->submesh_count);

				const uint32_t scene_prim_index = model_comp->submesh_first + cmd->submesh;
				const auto& scene_prims = m_scene.scene_primitives();

				HPR_ASSERT(scene_prim_index < scene_prims.size());

				const auto& primitive = scene_prims[scene_prim_index];

				if (auto* material_inst = m_resolver.resolve<rdr::MaterialInstance>(primitive.material)) {
					material_inst->albedo_tint      = cmd->albedo_tint;
					material_inst->metallic_factor  = cmd->metallic_factor;
					material_inst->roughness_factor = cmd->roughness_factor;
					material_inst->normal_scale     = cmd->normal_scale;
					material_inst->ao_strength      = cmd->ao_strength;
					material_inst->emissive_factor  = cmd->emissive_factor;
					material_inst->uv_scale         = cmd->uv_scale;
					material_inst->uv_offset        = cmd->uv_offset;
				}
				break;
			}
			default:
			{
				break;
			}
		}
	}
}


edt::InspectorSnapshot SceneLayer::selection_properties() const
{
	edt::InspectorSnapshot inspector_snapshot;

	const ecs::Entity entity = m_selection.entity;
	if (entity == ecs::invalid_entity) {
		return inspector_snapshot;
	}

	if (const auto* light = m_registry.get<ecs::LightComponent>(entity)) {
		inspector_snapshot.has_light = true;
		inspector_snapshot.light.enabled   = light->enabled ? 1 : 0;
		inspector_snapshot.light.color_rgb = light->color_rgb;
		inspector_snapshot.light.intensity = light->intensity;
		inspector_snapshot.light.range     = light->range;

		inspector_snapshot.light.inner_deg = std::cos(glm::radians(light->inner_deg));
		inspector_snapshot.light.outer_deg = std::cos(glm::radians(light->outer_deg));
	}

	if (const auto* material = m_registry.get<ecs::ModelComponent>(entity)) {
		inspector_snapshot.submesh_count = material->submesh_count;
		if (m_selection.submesh < material->submesh_count) {
			const auto& prim = m_scene.scene_primitives()[material->submesh_first + m_selection.submesh];
			inspector_snapshot.has_material = true;
			inspector_snapshot.material = prim.material;
		}
	}

	return inspector_snapshot;
}

} // hpr

