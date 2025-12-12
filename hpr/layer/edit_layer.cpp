#include "hpr/event/event_emitter.hpp"
#include "nuklear_cfg.hpp"

#include "edit_layer.hpp"
#include "render_data.hpp"
#include "event_dispatcher.hpp"
#include "draw_queue_data.hpp"
#include "inspector_state.hpp"
#include "command_stream.hpp"


namespace hpr {


void EditLayer::on_attach()
{
	const rdr::SurfaceInfo surface_info = m_renderer.surface_info();

	m_ui_context.init();
	m_ui_context.on_resize(surface_info.width, surface_info.height, surface_info.dpi);

	m_ui_backend.rebuild_default_font(surface_info.dpi);

	m_ui_style.apply_default(m_ui_context.backend_context(), m_ui_backend.default_font());

	m_ui_context.add_panel(&ui::ui_inspector, &m_inspector_state, true);
}


void EditLayer::on_detach()
{
	m_ui_context.shutdown();
	m_ui_style.shutdown();
}


bool EditLayer::on_event(Event& event)
{
	EventDispatcher dispatcher(event);

	dispatcher.dispatch<ResizeEvent>(
		[this](const ResizeEvent&) -> bool
		{
			const rdr::SurfaceInfo surface_info = m_renderer.surface_info();
			m_ui_context.on_resize(surface_info.width, surface_info.height, surface_info.dpi);
			return false;
		}
	);

	dispatcher.dispatch<SelectionChangedEvent>(
		[this](const SelectionChangedEvent& selection_event) -> bool
		{
			m_inspector_state.selection = selection_event.selection;

			const auto* provider = dynamic_cast<const edt::InspectorProvider*>(selection_event.emitter);
			if (!provider)
				return false;

			const edt::InspectorSnapshot inspector_snapshot = provider->selection_properties();
			m_inspector_state.submesh_count = inspector_snapshot.submesh_count;

			m_inspector_state.has_light = inspector_snapshot.has_light;
			if (inspector_snapshot.has_light) {
				m_inspector_state.light = inspector_snapshot.light;
			}

			m_inspector_state.has_material = inspector_snapshot.has_material;
			if (inspector_snapshot.has_material) {
				auto* mat_instance = m_editor_resolver.resolve<rdr::MaterialInstance>(inspector_snapshot.material);
				if (mat_instance) {
					m_inspector_state.albedo_tint   = mat_instance->albedo_tint;
					m_inspector_state.metallic      = mat_instance->metallic_factor;
					m_inspector_state.roughness     = mat_instance->roughness_factor;
					m_inspector_state.ao_strength   = mat_instance->ao_strength;
					m_inspector_state.normal_scale  = mat_instance->normal_scale;
					m_inspector_state.emissive      = mat_instance->emissive_factor;
					m_inspector_state.uv_scale      = mat_instance->uv_scale;
					m_inspector_state.uv_offset     = mat_instance->uv_offset;

					uint32_t map_mask = mat_instance->map_mask;
					if (auto* mat_template = m_editor_resolver.resolve<rdr::MaterialTemplate>(mat_instance->mat_template)) {
						map_mask |= mat_template->map_mask;
					}

					m_inspector_state.has_albedo   = (map_mask & rdr::Tex::albedo)   != 0;
					m_inspector_state.has_ormh     = (map_mask & rdr::Tex::ormh)     != 0;
					m_inspector_state.has_normal   = (map_mask & rdr::Tex::normal)   != 0;
					m_inspector_state.has_emissive = (map_mask & rdr::Tex::emissive) != 0;
				}
			}

			m_inspector_state.transform_dirty = false;
			m_inspector_state.light_dirty     = false;
			m_inspector_state.material_dirty  = false;

			return false;
		}
	);

	return false;
}


bool EditLayer::on_actions(std::span<const Action> actions)
{
	m_ui_context.sync_input_screen(m_input_state);

	for (const Action& action : actions) {
		if (action.kind == ActionKind::SelectClick) {

			if (m_ui_context.wants_mouse()) {
				return true;
			}

			return false;
		}
	}

	return false;
}


void EditLayer::on_update(float delta_time)
{
	m_ui_context.frame(delta_time);

	if (!m_cmd_stream)
		return;

	const bool has_entity = m_inspector_state.selection.entity != ecs::invalid_entity;
	if (!has_entity)
		return;

	if (m_inspector_state.transform_dirty) {

		SetTransform cmd;
		cmd.entity    = m_inspector_state.selection.entity;
		cmd.transform = m_inspector_state.selection.transform;

		const bool is_push_ok = m_cmd_stream->push(CmdType::SetTransform, cmd);
		HPR_ASSERT(is_push_ok);

		m_inspector_state.transform_dirty = false;
	}

	if (m_inspector_state.light_dirty && m_inspector_state.has_light) {

		SetLight cmd;
		cmd.entity = m_inspector_state.selection.entity;
		cmd.light  = m_inspector_state.light;

		const bool is_push_ok = m_cmd_stream->push(CmdType::SetLight, cmd);
		HPR_ASSERT(is_push_ok);

		m_inspector_state.light_dirty = false;
	}

	if (m_inspector_state.material_dirty && m_inspector_state.has_material) {

		SetMaterial cmd;
		cmd.entity  = m_inspector_state.selection.entity;
		cmd.submesh = m_inspector_state.selection.submesh;

		// TODO: optimize snapshot traffic, consider operational commands

		cmd.albedo_tint      = m_inspector_state.albedo_tint;
		cmd.metallic_factor  = m_inspector_state.metallic;
		cmd.roughness_factor = m_inspector_state.roughness;
		cmd.normal_scale     = m_inspector_state.normal_scale;
		cmd.ao_strength      = m_inspector_state.ao_strength;
		cmd.emissive_factor  = m_inspector_state.emissive;
		cmd.uv_scale         = m_inspector_state.uv_scale;
		cmd.uv_offset        = m_inspector_state.uv_offset;

		const bool is_push_ok = m_cmd_stream->push(CmdType::SetMaterial, cmd);
		HPR_ASSERT(is_push_ok);

		m_inspector_state.material_dirty = false;
	}
}


void EditLayer::on_submit(rdr::Renderer& renderer, uint32_t layer_index)
{
	rdr::UiDrawCommand submission {
		m_ui_context.backend_context(),
		layer_index,
		m_ui_backend.default_font_texture(),
		m_ui_backend.null_texture()
	};

	renderer.ui_queue().push(submission);
}


void EditLayer::on_result(Event& event)
{}

} // hpr

