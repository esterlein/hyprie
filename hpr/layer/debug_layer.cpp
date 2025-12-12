#include "debug_layer.hpp"
#include "draw_queue_data.hpp"

#include <cstdio>


namespace hpr {


DebugLayer::DebugLayer(rdr::Renderer& renderer, ui::UiBackend& ui_backend)
	: m_renderer   {renderer}
	, m_ui_backend {ui_backend}
{
	m_fps_label[0] = '\0';

	m_style.origin       = vec2(8.0f, 8.0f);
	m_style.line_height  = 16.0f;
	m_style.line_spacing = 2.0f;
	m_style.text_alpha   = 0.5f;

	m_style.color_header = vec4(1.0f, 1.0f, 1.0f, 0.9f);
	m_style.color_fps    = vec4(0.8f, 1.0f, 0.8f, 1.0f);
	m_style.color_info   = vec4(0.8f, 0.8f, 0.8f, 1.0f);
	m_style.color_warn   = vec4(1.0f, 0.9f, 0.5f, 1.0f);
	m_style.color_error  = vec4(1.0f, 0.5f, 0.5f, 1.0f);
	m_style.color_trace  = vec4(0.6f, 0.6f, 0.6f, 1.0f);

	m_style.update_packed();
}


void DebugLayer::on_attach()
{}


void DebugLayer::on_detach()
{}


bool DebugLayer::on_event(Event& event)
{
	(void) event;
	return false;
}


bool DebugLayer::on_actions(std::span<const Action> actions)
{
	bool consumed = false;

	for (const Action& action : actions) {
		switch (action.kind) {
		case ActionKind::DebugToggleOverlay:
			m_visible = !m_visible;
			consumed = true;
			break;

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
			consumed = true;
			break;

		case ActionKind::DebugToggleCore:
			m_category_mask ^= (1U << 0);
			consumed = true;
			break;

		case ActionKind::DebugToggleRender:
			m_category_mask ^= (1U << 1);
			consumed = true;
			break;

		case ActionKind::DebugToggleScene:
			m_category_mask ^= (1U << 2);
			consumed = true;
			break;

		case ActionKind::DebugToggleAsset:
			m_category_mask ^= (1U << 3);
			consumed = true;
			break;

		default:
			break;
		}
	}

	return consumed;
}


void DebugLayer::on_update(float delta_time)
{
	if (delta_time <= 0.0f)
		return;

	float fps_now = 1.0f / delta_time;

	float alpha = 0.1f;
	if (m_fps_smoothed == 0.0f)
		m_fps_smoothed = fps_now;
	else
		m_fps_smoothed = m_fps_smoothed + alpha * (fps_now - m_fps_smoothed);

	m_log_count = log::copy_ring_entries(m_log_entries, k_max_logs_per_frame);
}


void DebugLayer::on_submit(rdr::Renderer& renderer, uint32_t layer_index)
{
	if (!m_visible)
		return;

	rdr::DebugDrawCommand submission {};
	submission.layer_index  = layer_index;
	submission.line_count   = 0;
	submission.font_texture = m_ui_backend.default_font_texture();

	float text_pos_x = m_style.origin.x;
	float line_pos_y = m_style.origin.y;

	float time_ms = m_fps_smoothed > 0.0f ? 1000.0f / m_fps_smoothed : 0.0f;
	std::snprintf(m_fps_label, sizeof(m_fps_label), "fps: %.1f (%.3f ms)", m_fps_smoothed, time_ms);

	rdr::DebugTextLine& fps_line = submission.lines[submission.line_count++];
	fps_line.position = vec2(text_pos_x, line_pos_y);
	fps_line.color    = m_style.packed_color_fps;
	fps_line.text     = m_fps_label;

	line_pos_y += m_style.line_height + m_style.line_spacing;

	rdr::DebugTextLine& hdr_line = submission.lines[submission.line_count++];
	hdr_line.position = vec2(text_pos_x, line_pos_y);
	hdr_line.color    = m_style.packed_color_header;
	hdr_line.text     = "logs:";

	line_pos_y += m_style.line_height + m_style.line_spacing;

	for (size_t i = 0; i < m_log_count; ++i) {

		if (submission.line_count >= rdr::cfg::max_debug_text_lines)
			break;

		const log::LogEntry& log_entry = m_log_entries[i];

		if (static_cast<uint8_t>(log_entry.level) > static_cast<uint8_t>(m_min_level))
			continue;

		uint32_t lane = static_cast<uint32_t>(log_entry.category);
		uint32_t bit  = 1U << lane;

		if ((m_category_mask & bit) == 0U)
			continue;

		rdr::DebugTextLine& line = submission.lines[submission.line_count++];
		line.position = vec2(text_pos_x, line_pos_y);

		switch (log_entry.level) {
		case log::LogLevel::warn:
			line.color = m_style.packed_color_warn;
			break;
		case log::LogLevel::error:
		case log::LogLevel::fatal:
			line.color = m_style.packed_color_error;
			break;
		case log::LogLevel::trace:
			line.color = m_style.packed_color_trace;
			break;
		case log::LogLevel::info:
		case log::LogLevel::debug:
		default:
			line.color = m_style.packed_color_info;
			break;
		}

		line.text = log_entry.text;

		line_pos_y += m_style.line_height + m_style.line_spacing;
	}

	if (submission.line_count > 0)
		renderer.debug_queue().push(submission);
}

} // hpr

