#include "debug_layer.hpp"

#include "log.hpp"
#include "action.hpp"

#include "font_data.hpp"
#include "scene_context.hpp"
#include "stats.hpp"

#include <cstdio>


namespace hpr {


namespace cfg {

float delta_peak_window_sec = 5.0f;

} // hpr::cfg


DebugLayer::DebugLayer(
	const rdr::HudFontPresets&           font_presets,
	const FontResolver&                  font_resolver,
	rdr::SurfaceInfo                     surface_info,
	rdr::RenderQueue<rdr::DebugDrawCmd>& cmd_queue,
	rdr::Renderer&                       renderer,
	log::StatsHarvester&                 harvester

)
	: m_font_presets  {font_presets}
	, m_font_resolver {font_resolver}
	, m_surface_info  {surface_info}
	, m_cmd_queue     {cmd_queue}
	, m_renderer      {renderer}
	, m_harvester     {harvester}
{
	m_hud_style.text_alpha   = 1.0f;

	m_hud_style.color_header = vec4(1.0f, 1.0f, 1.0f, 0.9f);
	m_hud_style.color_fps    = vec4(0.5f, 0.5f, 1.0f, 1.0f);
	m_hud_style.color_info   = vec4(0.8f, 0.8f, 0.8f, 1.0f);
	m_hud_style.color_warn   = vec4(1.0f, 0.9f, 0.5f, 1.0f);
	m_hud_style.color_error  = vec4(1.0f, 0.5f, 0.5f, 1.0f);
	m_hud_style.color_trace  = vec4(0.6f, 0.6f, 0.6f, 1.0f);

	m_hud_style.update_packed();
}


void DebugLayer::on_attach()
{
	rebuild_layout();
}


void DebugLayer::on_detach()
{}


bool DebugLayer::on_event(Event& event)
{
	(void) event;
	return false;
}


bool DebugLayer::on_actions(const scn::SceneContext& scene_ctx, std::span<const Action> actions)
{
	(void) scene_ctx;

	bool action_consumed = false;

	for (const Action& action : actions) {
		switch (action.kind) {

		case ActionKind::DebugToggleOverlay:
			m_visible = !m_visible;
			action_consumed = true;
			break;

		/* log level */

		case ActionKind::DebugCycleLogLevel:
			switch (m_min_level) {
			case log::LogLevel::error:
				m_min_level = log::LogLevel::warn;
				break;
			case log::LogLevel::warn:
				m_min_level = log::LogLevel::info;
				break;
			case log::LogLevel::info:
				m_min_level = log::LogLevel::debug;
				break;
			case log::LogLevel::debug:
				m_min_level = log::LogLevel::trace;
				break;
			case log::LogLevel::trace:
				m_min_level = log::LogLevel::error;
				break;
			case log::LogLevel::fatal:
			default:
				m_min_level = log::LogLevel::error;
				break;
			}
			action_consumed = true;
			break;

		/* log category toggle */

		case ActionKind::DebugToggleCore:
			m_category_mask ^= (1U << 0);
			action_consumed = true;
			break;

		case ActionKind::DebugToggleRender:
			m_category_mask ^= (1U << 1);
			action_consumed = true;
			break;

		case ActionKind::DebugToggleScene:
			m_category_mask ^= (1U << 2);
			action_consumed = true;
			break;

		case ActionKind::DebugToggleAsset:
			m_category_mask ^= (1U << 3);
			action_consumed = true;
			break;

		/* log scroll */

		case ActionKind::DebugScrollUp:
			m_log_offset = std::min(
				m_log_offset + 1,
				log::cfg::log_ring_capacity - 1
			);
			break;

		case ActionKind::DebugScrollDown:
			m_log_offset = (m_log_offset >= 1)
				? m_log_offset - 1
				: 0;
			break;

		default:
			break;
		}
	}

	return action_consumed;
}


void DebugLayer::on_update(scn::SceneContext& scene_ctx, float delta_time)
{
	if (delta_time <= 0.0f)
		return;

	m_debug_stats.fps_now = 1.0f / delta_time;

	if (m_debug_stats.fps_smooth < 0.0f) {
		m_debug_stats.fps_smooth = m_debug_stats.fps_now;
	}
	else {
		m_debug_stats.fps_smooth =
			m_debug_stats.fps_smooth + 0.1f * (m_debug_stats.fps_now - m_debug_stats.fps_smooth);
	}

	m_debug_stats.delta_ms = delta_time * 1000.0f;

	m_debug_stats.peak_ms = std::max(m_debug_stats.peak_ms, m_debug_stats.delta_ms);

	m_debug_stats.peak_timer += delta_time;
	if (m_debug_stats.peak_timer >= cfg::delta_peak_window_sec) {
		m_debug_stats.peak_timer  = 0.0f;
		m_debug_stats.peak_ms     = m_debug_stats.delta_ms;
	}
}


void DebugLayer::on_submit(const scn::SceneContext& scene_ctx, uint32_t layer_idx)
{
	if (!m_visible)
		return;

	rdr::DebugDrawCmd submission {};

	submission.sort_key   =
		(static_cast<uint64_t>(layer_idx) << 32) |
		(static_cast<uint32_t>(m_hud_layout.canvas.font.index) & 0xFFFFFFFF);

	submission.layer_idx = layer_idx;
	submission.font      = m_hud_layout.canvas.font;

	/* stats widget */

	{
		struct StatField
		{
			std::array<char, 16> label {};
			std::array<char, 16> value {};
		};

		std::array<StatField, 20> fields {};
		size_t field_count = 0;

		auto push_field =
			[&fields, &field_count](const char* label, const char* format, auto... args) -> void
			{
				if (field_count >= fields.size())
					return;

				std::snprintf(
					fields[field_count].label.data(),
					fields[field_count].label.size(),
					"%-8s",
					label
				);

				std::snprintf(
					fields[field_count].value.data(),
					fields[field_count].value.size(),
					format,
					args...
				);

				++field_count;
			};

		const auto& frame_stats = m_harvester.frame_prev();
		const auto& scn_stats   = m_harvester.scene_layer_prev();

		push_field("FPS",    "%.1f", m_debug_stats.fps_smooth);
		push_field("FMS",    "%.3f", static_cast<double>(m_debug_stats.delta_ms));
		push_field("PEAK",   "%.3f", static_cast<double>(m_debug_stats.peak_ms));
		push_field("", "");

		push_field("SYNC MS", "%.3f", static_cast<double>(frame_stats.rdr_cpu_time));
		push_field("FRAME",   "%llu", static_cast<unsigned long long>(frame_stats.frame_index));
		push_field("", "");
		push_field("", "");

		push_field("DRAWS", "%u",   frame_stats.total.draw_calls);
		push_field("TRIGS", "%llu", static_cast<unsigned long long>(frame_stats.total.triangles));
		push_field("IDXS",  "%llu", static_cast<unsigned long long>(frame_stats.total.indices));
		push_field("PRIMS", "%u",   frame_stats.total.submeshes);

		push_field("HIZ  MS", "%.3f", scn_stats.hiz_raster_ms);
		push_field("CULL MS", "%.3f", scn_stats.cull_job_ms);
		push_field("RAY  MS", "%.3f", scn_stats.ray_job_ms);
		push_field("", "");

		push_field("FRS TEST", "%u", scn_stats.frust_tested);
		push_field("FRS CULL", "%u", scn_stats.frust_culled);
		push_field("OCC TEST", "%u", scn_stats.occl_tested);
		push_field("OCC CULL", "%u", scn_stats.occl_culled);

		if (field_count > 0) {

			const int num_cols     = 5;
			const int rows_per_col = 4;
			
			const int start_y  = m_hud_layout.top.y;
			const int pair_gap = m_hud_layout.cell_w;

			const int edge_margin  = 9 * m_hud_layout.cell_w;
			const int usable_width = m_hud_layout.top.w - (2 * edge_margin);

			for (size_t i = 0; i < field_count; ++i) {
				const char* label = fields[i].label.data();
				const char* value = fields[i].value.data();

				if (value[0] == '\0') {
					continue;
				}

				const int label_len = static_cast<int>(std::strlen(label));
				const int label_w   = label_len * m_hud_layout.cell_w;

				const int col = static_cast<int>(i) / rows_per_col;
				const int row = static_cast<int>(i) % rows_per_col;

				const float t = (num_cols > 1) ? static_cast<float>(col) / (num_cols - 1) : 0.5f;

				const int slot_center_x = m_hud_layout.top.x + edge_margin + static_cast<int>(t * usable_width);
				const int slot_y        = start_y + row * m_hud_layout.cell_h;

				auto& label_line  = submission.lines.emplace_back();
				label_line.color  = m_hud_style.packed_color_fps;
				label_line.cell_x = slot_center_x - pair_gap / 2 - label_w;
				label_line.cell_y = slot_y;

				std::snprintf(
					label_line.text.data(),
					label_line.text.size(),
					"%s",
					label
				);

				auto& value_line  = submission.lines.emplace_back();
				value_line.color  = m_hud_style.packed_color_fps;
				value_line.cell_x = slot_center_x + pair_gap / 2;
				value_line.cell_y = slot_y;

				std::snprintf(
					value_line.text.data(),
					value_line.text.size(),
					"%s",
					value
				);
			}
		}
	}

	/* logger widget */

	{
		const int cell_x = m_hud_layout.log.x;
		int cell_y =
			m_hud_layout.log.y              +
			(m_hud_layout.log_rows_max - 1) *
			m_hud_layout.cell_h;

		int log_rows_left      = m_hud_layout.log_rows_max;
		const int log_cols_max = m_hud_layout.log_cols_max;

		std::lock_guard<std::mutex> lock(log::mutex());

		log::LogState& log_state = log::state();
		log::LogRing&  log_ring  = log_state.ring;
		const size_t   ring_head = log_ring.head;

		for (size_t log_row_idx = 0; log_row_idx < m_hud_layout.log_rows_max; ++log_row_idx) {

			const size_t ring_idx =
				(ring_head + log::cfg::log_ring_capacity - 1 - log_row_idx - m_log_offset)
					% log::cfg::log_ring_capacity;

			const log::LogEntry& log_entry = log_ring.entries[ring_idx];

			const uint32_t category_bit = 1U << static_cast<uint32_t>(log_entry.category);
			if ((m_category_mask & category_bit) == 0U)
				continue;

			const size_t text_len    = std::strlen(log_entry.text);
			const size_t rows_needed = (text_len + m_hud_layout.log_cols_max - 1) / m_hud_layout.log_cols_max;

			if(log_rows_left - rows_needed < 0)
				break;

			if (static_cast<uint8_t>(log_entry.level) > static_cast<uint8_t>(m_min_level))
				continue;

			uint32_t color = m_hud_style.packed_color_info;

			switch (log_entry.level) {
			case log::LogLevel::warn:
				color = m_hud_style.packed_color_warn;
				break;
			case log::LogLevel::error:
			case log::LogLevel::fatal:
				color = m_hud_style.packed_color_error;
				break;
			case log::LogLevel::trace:
				color = m_hud_style.packed_color_trace;
				break;
			case log::LogLevel::info:
			case log::LogLevel::debug:
			default:
				color = m_hud_style.packed_color_info;
				break;
			}

			size_t text_pos = 0;

			int line_y = cell_y - static_cast<int>(rows_needed - 1) * m_hud_layout.cell_h;

			while (text_pos < text_len) {
				const size_t chars_left = text_len - text_pos;
				const size_t char_count =
					std::min(chars_left, static_cast<size_t>(log_cols_max));

				if (char_count == 0)
					break;

				auto& line  = submission.lines.emplace_back();
				line.color  = color;
				line.cell_x = cell_x;
				line.cell_y = line_y;

				std::memcpy(line.text.data(), log_entry.text + text_pos, char_count);
				line.text[char_count] = '\0';

				text_pos += char_count;
				line_y   += m_hud_layout.cell_h;
			}

			cell_y        -= static_cast<int>(rows_needed) * m_hud_layout.cell_h;
			log_rows_left -= static_cast<int>(rows_needed);

			if (log_rows_left <= 0)
				break;
		}
	}

	if (submission.lines.size() > 0)
		m_cmd_queue.push(std::move(submission));
}


void DebugLayer::rebuild_layout()
{
	const int framebuffer_w = static_cast<int>(m_surface_info.width);
	const int framebuffer_h = static_cast<int>(m_surface_info.height);

	ui::Canvas canvas = ui::fit_aspect(framebuffer_w, framebuffer_h, m_font_presets);

	const rdr::Font* font = m_font_resolver.resolve(canvas.font);

	m_hud_layout = ui::build_debug_layout(
		canvas,
		static_cast<int>(font->metrics.glyphs[0].advance_px),
		static_cast<int>(font->metrics.line_height),
		ui::cfg::debug_style
	);
}


} // hpr

