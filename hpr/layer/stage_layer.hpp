#pragma once

#include "hprint.hpp"
#include "mtp_memory.hpp"

#include "layer.hpp"
#include "action.hpp"
#include "surface.hpp"

#include "render_queue.hpp"
#include "draw_queue_data.hpp"

#include "scene_context.hpp"

#include "canonical.hpp"
#include "event_emitter.hpp"
#include "command_emitter.hpp"

#include "ecs_registry.hpp"
#include "systems_scene.hpp"
#include "systems_render.hpp"
#include "ecs_registry_types.hpp"

#include "sokol_app.h"

#include <span>


namespace hpr {


struct Event;
class  EventQueue;
struct CmdStream;


class StageLayer final : public Layer, public EventEmitter, public CommandEmitter
{
private:

	struct RoundPickProxy
	{
		ecs::Entity entity;
		vec2        center_px;
		float       radius_px;
	};

public:

	StageLayer(
		geo::CanonicalPrimitives               canonical,
		MainRegistry&                          registry,
		rdr::SurfaceInfo                       surface_info,
		rdr::StagingContext                    staging_ctx,
		rdr::RenderQueue<rdr::CueDrawCmd>&     cue_queue,
		rdr::RenderQueue<rdr::OverlayDrawCmd>& overlay_queue
	);

	void on_attach() override;
	void on_detach() override;

	bool on_event(Event& event) override;

	bool on_actions(const scn::SceneContext& scn_ctx, std::span<const Action> actions) override;
	void on_update(scn::SceneContext& scn_ctx, float delta_time) override;
	void on_submit(const scn::SceneContext& scn_ctx, uint32_t layer_idx) override;

	void on_result(Event& event) override;

	void set_event_queue(EventQueue& queue) override
	{
		m_event_queue = &queue;
	}

	void set_command_stream(CmdStream& stream) override
	{
		m_cmd_stream = &stream;
	}

private:

	geo::CanonicalPrimitives m_canonical {};

	MainRegistry&       m_registry;
	rdr::SurfaceInfo    m_surface_info;
	rdr::StagingContext m_staging_ctx;

	ecs::Entity m_entity {ecs::ctx::invalid_entity};

	EventQueue* m_event_queue {};
	CmdStream*  m_cmd_stream  {};

	mtp::vault<RoundPickProxy, mtp::default_set> m_pick_proxies;

	rdr::RenderQueue<rdr::CueDrawCmd>&     m_cue_queue;
	rdr::RenderQueue<rdr::OverlayDrawCmd>& m_overlay_queue;
};

} // hpr

