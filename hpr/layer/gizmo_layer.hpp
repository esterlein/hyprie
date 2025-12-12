#pragma once

#include <cstdint>
#include <limits>
#include <span>

#include "sokol_app.h"

#include "math.hpp"
#include "layer.hpp"
#include "entity.hpp"
#include "editor_data.hpp"
#include "event_emitter.hpp"
#include "command_emitter.hpp"


namespace hpr {
namespace rdr {

class Renderer;

} // hpr::rdr


struct Event;
class EventQueue;
class CmdStream;


class GizmoLayer final : public Layer, public EventEmitter, public CommandEmitter
{
public:

	GizmoLayer(rdr::Renderer& renderer, edt::GizmoPrimitives gizmo_primitives);

	void on_attach() override;
	void on_detach() override;

	bool on_event(Event& event) override;

	bool on_actions(std::span<const Action> actions) override;

	void on_update(float delta_time) override;
	void on_submit(rdr::Renderer& renderer, uint32_t layer_index) override;

	void on_result(Event& event) override;

	void set_event_queue(EventQueue& queue) override
	{ m_event_queue = &queue; }

	void set_command_stream(CmdStream& stream) override
	{ m_cmd_stream = &stream; }

private:

	rdr::Renderer&       m_renderer;
	edt::GizmoPrimitives m_gizmo_primitives;

	EventQueue* m_event_queue {};
	CmdStream*  m_cmd_stream  {};

	ecs::Entity m_entity {ecs::invalid_entity};

	vec3 m_position {};
	quat m_rotation {};
	vec3 m_scale    {1.0f, 1.0f, 1.0f};

	edt::GizmoMode m_mode {edt::GizmoMode::Translate};
	edt::TransformSpace m_space {edt::TransformSpace::World};

	edt::GizmoAxis m_hover_axis  {edt::GizmoAxis::None};
	edt::GizmoAxis m_active_axis {edt::GizmoAxis::None};

	bool m_active   {false};
	bool m_snapping {false};

	vec2 m_mouse_px       {};
	vec2 m_drag_start_px  {};
	vec2 m_drag_accum_px  {0.0f, 0.0f};

	vec3 m_drag_start_pos   {};
	quat m_drag_start_rot   {};
	vec3 m_drag_start_scale {};

	float m_screen_scale {1.0f};
};

} // hpr

