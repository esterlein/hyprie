#include <cmath>
#include <limits>

#include "camera_controller.hpp"
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


struct FrustumPlane
{
	vec3  normal;
	vec3  abs_normal;
	float offset;
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

	uint32_t    layer_index;
	ecs::Entity selected_entity;

	SceneLayer::DrawCmdAsyncResult* draw_cmd_result;
};


SceneLayer::SceneLayer(
	ECSRegistry&              ecs_registry,
	res::AssetKeeper&         asset_keeper,
	rdr::RenderForge&         render_forge,
	rdr::Renderer&            renderer,
	const io::InputBinding&   input_binding,
	scn::SceneResolver&       resolver,
	const char*               scene_path,
	mtp::shared<mtp_scn_set>& metapool
)
	: m_registry               {ecs_registry}
	, m_asset_keeper           {asset_keeper}
	, m_render_forge           {render_forge}
	, m_renderer               {renderer}
	, m_binding                {input_binding}
	, m_resolver               {resolver}
	, m_scene_path             {scene_path}
	, m_slice_draw_cmd_results {metapool}
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

	m_job_scheduler.init(worker_thread_count);
}


void SceneLayer::on_attach()
{
	scn::io::SceneDoc scene_doc {};
	if (!scn::io::read_file(m_scene_path, scene_doc)){
		HPR_FATAL(
			log::LogCategory::scene,
			"[layer][on_attach] read scene file failed"
		);
		return;
	}

	m_scene.clear();
	if (!scn::instantiate(scene_doc, m_registry, m_asset_keeper, m_render_forge, m_scene)) {
		HPR_FATAL(
			log::LogCategory::scene,
			"[layer][on_attach] instantiate scene failed"
		);
		return;
	}

	ecs::TransformSystem::update(m_registry);
	ecs::BoundSystem::update(m_registry);

	m_active_cam_entity = ecs::CameraSystem::find_active_camera(m_registry);
	HPR_ASSERT(m_active_cam_entity != ecs::invalid_entity);

	const bool is_cam_ok = ecs::CameraSystem::init_camera_controller(
		m_registry,
		m_active_cam_entity,
		m_cam_controller
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
			m_cam_controller.delta.orbit_x += payload.delta_x;
			m_cam_controller.delta.orbit_y += payload.delta_y;
			action_consumed = true;
		}
		break;
		case ActionKind::Pan: {
			const auto& payload = std::get<PanAction>(action.payload);
			m_cam_controller.delta.pan_x += payload.delta_x;
			m_cam_controller.delta.pan_y += payload.delta_y;
			action_consumed = true;
		}
		break;
		case ActionKind::Dolly: {
			const auto& payload = std::get<DollyAction>(action.payload);
			m_cam_controller.delta.dolly += payload.amount;
			action_consumed = true;
		}
		break;
		case ActionKind::Move: {
			const auto& payload = std::get<MoveAction>(action.payload);
			m_cam_controller.delta.move_forward = payload.forward;
			m_cam_controller.delta.move_right   = payload.right;
			m_cam_controller.delta.move_up      = payload.up;
			action_consumed = true;
		}
		break;

		case ActionKind::ToggleCameraMode: {
			m_cam_controller.mode =
				m_cam_controller.mode == scn::CameraController::Mode::iso
				? scn::CameraController::Mode::fly
				: scn::CameraController::Mode::iso;
			action_consumed = true;
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
	ecs::CameraSystem::update_camera_controller(
		m_registry,
		m_active_cam_entity,
		m_cam_controller,
		delta_time,
		m_binding.pan_sensitivity,
		m_binding.dolly_sensitivity
	);

	ecs::HierarchySystem::update(m_registry);
	ecs::TransformSystem::update(m_registry);
	ecs::BoundSystem::update(m_registry);

	const float aspect_ratio = m_renderer.surface_info().aspect;

	ecs::CameraSystem::build_view(
		m_registry,
		m_active_cam_entity,
		aspect_ratio,
		m_cam_controller,
		m_draw_view
	);

	m_draw_view_light_set.ambient_rgb = glm::make_vec3(m_scene.ambient());

	ecs::LightSystem::build_light(m_registry, m_draw_view, m_draw_view_light_set);
}


void SceneLayer::on_submit(rdr::Renderer& renderer, uint32_t layer_index)
{
	m_renderer.set_context(FrameContext {m_draw_view, m_draw_view_light_set});

	const auto& scene_primitives = m_scene.scene_primitives();

	std::array<FrustumPlane, math::frustum_plane_count> frustum_planes;

	for (size_t plane_idx = 0; plane_idx < frustum_planes.size(); ++plane_idx) {

		const vec4 raw_plane = m_draw_view.frustum[plane_idx];

		const vec3 raw_normal {
			raw_plane.x,
			raw_plane.y,
			raw_plane.z
		};

		const float normal_len = glm::length(raw_normal);

		const float inv_normal_len =
			(normal_len > 0.0f) ? (1.0f / normal_len) : 0.0f;

		const vec3 unit_normal = raw_normal * inv_normal_len;

		frustum_planes[plane_idx] = {
			unit_normal,
			glm::abs(unit_normal),
			raw_plane.w * inv_normal_len
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

			bool is_culled = false;

			for (const auto& plane : frustum_planes) {

				const float aabb_proj_radius =
					plane.abs_normal.x * aabb_half.x +
					plane.abs_normal.y * aabb_half.y +
					plane.abs_normal.z * aabb_half.z;

				const float signed_distance = glm::dot(plane.normal, aabb_center) + plane.offset;

				if (signed_distance < -aabb_proj_radius) {
					is_culled = true;
					break;
				}
			}

			if (is_culled) {
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

	m_slice_draw_cmd_results.resize(job_slice_count);

	for (uint32_t job_slice_idx = 0; job_slice_idx < job_slice_count; ++job_slice_idx) {

		auto& slice = draw_cmd_job_slices[job_slice_idx];

		slice.instances           = model_draw_instances.data();
		slice.instance_count      = model_instance_count;
		slice.scene_primitives    = &scene_primitives;
		slice.frustum_planes      = frustum_planes.data();
		slice.frustum_plane_count = static_cast<uint32_t>(frustum_planes.size());
		slice.layer_index         = layer_index;
		slice.selected_entity     = m_selection.entity;

		slice.draw_cmd_result = &m_slice_draw_cmd_results[job_slice_idx];
		slice.draw_cmd_result->count = 0;
	}

	job::JobLatch job_latch;

	m_job_scheduler.dispatch_range(
		job_latch,
		&build_model_draw_cmds,
		model_instance_count,
		cfg::job_grain,
		draw_cmd_job_slices.data()
	);

	job_latch.wait();

	for (uint32_t job_slice_idx = 0; job_slice_idx < job_slice_count; ++job_slice_idx) {
		for (const auto& draw_cmd : m_slice_draw_cmd_results[job_slice_idx]) {
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

	auto& slice_draw_cmds = *slice->draw_cmd_result;
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
				glm::dot(plane.normal, aabb_center) + plane.offset;

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

			slice->draw_cmd_result->push(draw_cmd);
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

