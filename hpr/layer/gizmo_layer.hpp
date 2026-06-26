#pragma once

#include "hprint.hpp"

#include "math.hpp"
#include "layer.hpp"
#include "entity.hpp"
#include "surface.hpp"
#include "scene_context.hpp"

#include "editor_data.hpp"
#include "render_context.hpp"

#include "event_emitter.hpp"
#include "command_emitter.hpp"

#include "render_queue.hpp"
#include "draw_queue_data.hpp"

#include "sokol_app.h"

#include <span>


namespace hpr {


struct Event;
class  EventQueue;
struct CmdStream;


class GizmoLayer final : public Layer, public EventEmitter, public CommandEmitter
{
public:

	GizmoLayer(
		edt::GizmoPrimitives                   gizmo_primitives,
		rdr::SurfaceInfo                       surface_info,
		rdr::StagingContext                    staging_ctx,
		rdr::RenderQueue<rdr::OverlayDrawCmd>& cmd_queue
	);

	void on_attach() override;
	void on_detach() override;

	bool on_event(Event& event) override;

	bool on_actions(const scn::SceneContext& scn_ctx, std::span<const Action> actions) override;
	void on_update(scn::SceneContext& scn_ctx, float delta_time) override;
	void on_submit(const scn::SceneContext& scn_ctx, uint32_t layer_idx) override;

	void on_result(Event& event) override;

	void set_event_queue(EventQueue& queue) override
	{ m_event_queue = &queue; }

	void set_command_stream(CmdStream& stream) override
	{ m_cmd_stream = &stream; }

private:

	edt::GizmoPrimitives m_gizmo_primitives;
	rdr::SurfaceInfo     m_surface_info;
	rdr::StagingContext  m_staging_ctx;

	rdr::RenderQueue<rdr::OverlayDrawCmd>& m_cmd_queue;

	EventQueue* m_event_queue {};
	CmdStream*  m_cmd_stream  {};

	ecs::Entity m_entity {ecs::ctx::invalid_entity};

	vec3 m_position {};
	quat m_rotation {};
	vec3 m_scale    {1.0f, 1.0f, 1.0f};

	edt::GizmoMode m_mode {edt::GizmoMode::Translate};
	edt::TransformSpace m_space {edt::TransformSpace::World};

	edt::GizmoAxis m_hover_axis  {edt::GizmoAxis::None};
	edt::GizmoAxis m_active_axis {edt::GizmoAxis::None};

	bool m_active   {false};
	bool m_snapping {false};

	vec2 m_mouse_px      {};
	vec2 m_drag_start_px {};
	vec2 m_drag_accum_px {0.0f, 0.0f};

	vec3 m_drag_start_pos   {};
	quat m_drag_start_rot   {};
	vec3 m_drag_start_scale {};

	float m_screen_scale {1.0f};
};

} // hpr

