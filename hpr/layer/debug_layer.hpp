#pragma once

#include "math.hpp"

#include "log.hpp"
#include "stats.hpp"
#include "event.hpp"
#include "layer.hpp"

#include "font_data.hpp"
#include "renderer.hpp"
#include "scene_context.hpp"

#include "render_queue.hpp"
#include "draw_queue_data.hpp"

#include "surface.hpp"
#include "ui_core.hpp"
#include "handle_resolver.hpp"

#include <span>


namespace hpr {


using FontResolver = res::HandleResolver <
	res::ResolverEntry<rdr::Font, const res::HandleStore<rdr::Font>>
>;


class DebugLayer : public Layer
{
public:

	DebugLayer(
		const rdr::HudFontPresets&           font_presets,
		const FontResolver&                  font_resolver,
		rdr::SurfaceInfo                     surface_info,
		rdr::RenderQueue<rdr::DebugDrawCmd>& cmd_queue,
		rdr::Renderer&                       renderer,
		log::StatsHarvester&                 harvester
	);

public:

	struct HudColorStyle
	{
		float text_alpha;

		vec4 color_header;
		vec4 color_fps;
		vec4 color_info;
		vec4 color_warn;
		vec4 color_error;
		vec4 color_trace;

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

			return (packed_a << 24) | (packed_b << 16) | (packed_g << 8) | packed_r;
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

	void on_attach() override;
	void on_detach() override;
	bool on_event(Event& event) override;

	bool on_actions(const scn::SceneContext& scn_ctx, std::span<const Action> actions) override;
	void on_update(scn::SceneContext& scn_ctx, float delta_time) override;
	void on_submit(const scn::SceneContext& scn_ctx, uint32_t layer_idx) override;

private:

	struct DebugStats
	{
		float fps_now    { 0.0f};
		float fps_smooth {-1.0f};
		float delta_ms   { 0.0f};
		float peak_ms    { 0.0f};
		float peak_timer { 0.0f};
	} m_debug_stats {};

	void rebuild_layout();

private:

	bool          m_visible       {false};
	log::LogLevel m_min_level     {log::LogLevel::trace};
	uint32_t      m_category_mask {0x0FU};
	size_t        m_log_offset    {0};

	ui::DebugLayout     m_hud_layout   {};
	HudColorStyle       m_hud_style    {};
	rdr::HudFontPresets m_font_presets {};

	const FontResolver m_font_resolver;

	rdr::SurfaceInfo m_surface_info;

	rdr::RenderQueue<rdr::DebugDrawCmd>& m_cmd_queue;
	rdr::Renderer&                       m_renderer;
	log::StatsHarvester&                 m_harvester;
};

} // hpr

