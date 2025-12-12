#pragma once

#include <cstddef>
#include <span>
#include <cstring>

#include "log.hpp"
#include "event.hpp"
#include "layer.hpp"
#include "action.hpp"
#include "renderer.hpp"
#include "ui_backend.hpp"
#include "draw_queue_data.hpp"


namespace hpr {


class DebugLayer : public Layer
{
public:

	struct DebugStream
	{
		const char* category;
		bool        enabled;
	};

	struct OverlayStyle
	{
		vec2  origin;
		float line_height;
		float line_spacing;
		float text_alpha;
		vec4  color_header;
		vec4  color_fps;
		vec4  color_info;
		vec4  color_warn;
		vec4  color_error;
		vec4  color_trace;

		uint32_t packed_color_header;
		uint32_t packed_color_fps;
		uint32_t packed_color_info;
		uint32_t packed_color_warn;
		uint32_t packed_color_error;
		uint32_t packed_color_trace;

		uint32_t pack_color(const vec4& color) const
		{
			float a = color.a * text_alpha;

			if (a < 0.0f) a = 0.0f;
			if (a > 1.0f) a = 1.0f;

			float r = color.r;
			float g = color.g;
			float b = color.b;

			if (r < 0.0f) r = 0.0f;
			if (r > 1.0f) r = 1.0f;
			if (g < 0.0f) g = 0.0f;
			if (g > 1.0f) g = 1.0f;
			if (b < 0.0f) b = 0.0f;
			if (b > 1.0f) b = 1.0f;

			uint32_t packed_r = static_cast<uint32_t>(r * 255.0f + 0.5f);
			uint32_t packed_g = static_cast<uint32_t>(g * 255.0f + 0.5f);
			uint32_t packed_b = static_cast<uint32_t>(b * 255.0f + 0.5f);
			uint32_t packed_a = static_cast<uint32_t>(a * 255.0f + 0.5f);

			return (packed_r << 24) | (packed_g << 16) | (packed_b << 8) | packed_a;
		}

		void update_packed()
		{
			packed_color_header = pack_color(color_header);
			packed_color_fps    = pack_color(color_fps);
			packed_color_info   = pack_color(color_info);
			packed_color_warn   = pack_color(color_warn);
			packed_color_error  = pack_color(color_error);
			packed_color_trace  = pack_color(color_trace);
		}
	};

	DebugLayer(rdr::Renderer& renderer, ui::UiBackend& ui_backend);

	void on_attach() override;
	void on_detach() override;
	bool on_event(Event& event) override;
	bool on_actions(std::span<const Action> actions) override;
	void on_update(float delta_time) override;
	void on_submit(rdr::Renderer& renderer, uint32_t layer_index) override;

private:

	static constexpr size_t k_max_streams        {16};
	static constexpr size_t k_max_logs_per_frame {128};

	rdr::Renderer& m_renderer;
	ui::UiBackend& m_ui_backend;

	bool          m_visible       {false};
	log::LogLevel m_min_level     {log::LogLevel::debug};
	uint32_t      m_category_mask {0x0FU};

	float m_fps_smoothed {0.0f};

	OverlayStyle m_style {};

	log::LogEntry m_log_entries[k_max_logs_per_frame] {};
	size_t        m_log_count {0};

	DebugStream m_streams[k_max_streams] {};
	size_t m_stream_count {0};

	char m_fps_label[64];
};

} // hpr

