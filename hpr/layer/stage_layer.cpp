#include "stage_layer.hpp"

#include "pixel_utils.hpp"
#include "canonical_data.hpp"
#include "render_context.hpp"
#include "scene_context.hpp"
#include "surface.hpp"


namespace hpr {


StageLayer::StageLayer(
	geo::CanonicalPrimitives               canonical,
	MainRegistry&                          registry,
	rdr::SurfaceInfo                       surface_info,
	rdr::StagingContext                    staging_ctx,
	rdr::RenderQueue<rdr::CueDrawCmd>&     cue_queue,
	rdr::RenderQueue<rdr::OverlayDrawCmd>& overlay_queue
)
	: m_canonical     {std::move(canonical)}
	, m_registry      {registry}
	, m_surface_info  {surface_info}
	, m_staging_ctx   {staging_ctx}
	, m_cue_queue     {cue_queue}
	, m_overlay_queue {overlay_queue}
{}

void StageLayer::on_attach()
{}

void StageLayer::on_detach()
{}

bool StageLayer::on_event(Event& event)
{
	return false;
}


bool StageLayer::on_actions(const scn::SceneContext& scene_ctx, std::span<const Action> actions)
{
	(void) scene_ctx;

	bool action_consumed = false;
	for (const Action& action : actions) {
		if (action.kind != ActionKind::SelectClick) continue;

		const auto& payload = std::get<SelectClickAction>(action.payload);
		const vec2 mouse{payload.x, payload.y};

		for (size_t i = m_pick_proxies.size(); i > 0; --i) {
			const auto& proxy = m_pick_proxies[i - 1];
			
			const vec2 diff       = mouse - proxy.center_px;
			const float dist_sq   = glm::dot(diff, diff);
			const float radius_sq = proxy.radius_px * proxy.radius_px;

			if (dist_sq <= radius_sq) {
				auto* transform = m_registry.get<ecs::TransformComponent>(proxy.entity);
				HPR_ASSERT(transform);

				auto* event = m_event_queue->push<SelectionChangedEvent>();

				event->selection.transform.position = transform->position;
				event->selection.transform.rotation = transform->rotation;
				event->selection.transform.scale    = transform->scale;
				event->selection.entity       = proxy.entity;
				event->selection.submesh      = std::numeric_limits<uint32_t>::max();
				event->emitter                = this;

				action_consumed = true;
				break;
			}
		}
		if (action_consumed) {
			break;
		}
	}
	return action_consumed;
}


void StageLayer::on_update(scn::SceneContext& scene_ctx, float delta_time)
{
	m_pick_proxies.clear();
	m_staging_ctx.cue_trs_mass->clear();
	m_staging_ctx.orl_trs_mass->clear();
}


void StageLayer::on_submit(const scn::SceneContext& scene_ctx, uint32_t layer_idx)
{
	const auto& view   = scene_ctx.draw_view;
	const float width  = static_cast<float>(m_surface_info.width);
	const float height = static_cast<float>(m_surface_info.height);

	const vec3 cam_right = {view.mtx_V[0][0], view.mtx_V[1][0], view.mtx_V[2][0]};

	auto& cue_trs_mass = m_staging_ctx.cue_trs_mass;
	auto& orl_trs_mass = m_staging_ctx.orl_trs_mass;

	const auto& box_submesh =
		m_canonical.geo_slice[static_cast<uint32_t>(geo::CanonicalSubmesh::Box)];

	m_registry.template scan<ecs::LightComponent, ecs::TransformComponent>(
		[
			this,
			&view,
			width,
			height,
			&cam_right,
			&cue_trs_mass,
			&orl_trs_mass,
			&box_submesh,
			layer_idx
		](
			ecs::Entity                    entity,
			const ecs::LightComponent&     light,
			const ecs::TransformComponent& transform
		)
		{
			mat4 mtx_M = glm::translate(mat4(1.0f), transform.position);

			const vec2 edge_px =
				rdr::pos_world_to_px(transform.position + cam_right, view.mtx_VP, width, height);
			const vec2 center_px =
				rdr::pos_world_to_px(transform.position, view.mtx_VP, width, height);
			const float radius_px = glm::distance(center_px, edge_px);

			m_pick_proxies.push_back({
				.entity    = entity,
				.center_px = center_px,
				.radius_px = radius_px
			});

			const uint32_t cue_trs_idx =
				cue_trs_mass->push_raw({glm::scale(mtx_M, vec3(2.0f))});
			const uint32_t orl_trs_idx =
				orl_trs_mass->push_raw({glm::scale(mtx_M, vec3(0.5f)), vec4(light.color_rgb, 1.0f)});

			const uint32_t cue_mask      = 1;
			const uint32_t tilemap_slice = 0U;
			const uint32_t palette_slice = 0U;

			const uint32_t vtx_base  = m_canonical.vtx_base  + box_submesh.vtx_base;
			const uint32_t idx_first = m_canonical.idx_first + box_submesh.idx_first;

			const uint64_t cue_sort_key =
				(static_cast<uint64_t>(cue_mask      & 0xFFFFU)   << 48) |
				(static_cast<uint64_t>(vtx_base      & 0xFFFFU)   << 32) |
				(static_cast<uint64_t>(idx_first     & 0xFFFFFFU) << 8)  |
				(static_cast<uint64_t>(palette_slice & 0x0FU)     << 4)  |
				(static_cast<uint64_t>(tilemap_slice & 0x0FU));

			m_cue_queue.push(rdr::CueDrawCmd {
				.sort_key      = cue_sort_key,
				.layer_idx     = layer_idx,
				.vtx_base      = m_canonical.vtx_base,
				.idx_first     = idx_first,
				.idx_count     = box_submesh.idx_count,
				.trs_idx       = cue_trs_idx,
				.cue_mask      = cue_mask,
				.tilemap_slice = tilemap_slice,
				.palette_slice = palette_slice
			});

			const uint32_t overlay_flag = 0U;

			const uint64_t orl_sort_key =
					(static_cast<uint64_t>(overlay_flag          & 0xFFFFU)   << 48) |
					(static_cast<uint64_t>(m_canonical.vtx_base  & 0xFFFFU)   << 32) |
					(static_cast<uint64_t>(idx_first             & 0xFFFFFFU) << 8)  |
					(static_cast<uint64_t>(box_submesh.idx_count & 0xFFU));

			m_overlay_queue.push(rdr::OverlayDrawCmd {
				.sort_key  = orl_sort_key,
				.layer_idx = layer_idx,
				.vtx_base  = m_canonical.vtx_base,
				.idx_first = idx_first,
				.idx_count = box_submesh.idx_count,
				.trs_idx   = orl_trs_idx,
				.flags     = overlay_flag
			});
		}
	);
}


void StageLayer::on_result(Event& event)
{}


} // hpr

