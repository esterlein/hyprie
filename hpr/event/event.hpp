#pragma once

#include <cstdint>

#include "math.hpp"
#include "entity.hpp"
#include "event_emitter.hpp"
#include "render_data.hpp"
#include "scene_data.hpp"


namespace hpr {


class Layer;


enum class EventType
{
	None = 0,
	Raycast,
	InspectorQuery,
	SelectionChanged,
	Resize,
};


struct Event
{
	virtual ~Event() = default;

	bool handled {false};

	EventEmitter* emitter {nullptr};

	virtual EventType get_event_type() const = 0;
	virtual const char* get_name() const = 0;

	static EventType get_static_type()
	{ return EventType::None; }
};


struct SelectionChangedEvent : public Event
{
	scn::Selection selection {};
	bool additive {false};

	EventType get_event_type() const override
	{ return get_static_type(); }

	const char* get_name() const override
	{ return "SelectionChanged"; }

	static EventType get_static_type()
	{ return EventType::SelectionChanged; }
};


struct RaycastEvent : public Event
{
	float mouse_x;
	float mouse_y;

	ecs::Entity entity {ecs::invalid_entity};
	uint32_t submesh   {0};

	EventType get_event_type() const override
	{ return get_static_type(); }

	const char* get_name() const override
	{ return "Raycast"; }

	static EventType get_static_type()
	{ return EventType::Raycast; }
};


struct InspectorQueryEvent : public Event
{
	using MatHandle = Handle<rdr::MaterialInstance>;

	ecs::Entity entity        {ecs::invalid_entity};
	uint32_t    submesh       {0};
	uint32_t    submesh_count {0};

	bool has_transform {false};
	scn::Transform transform {};

	bool has_light {false};

	// NOTE: questionable
	//scn::RenderLight light {};

	bool has_material  {false};
	MatHandle material {MatHandle::null()};

	EventType get_event_type() const override
	{ return get_static_type(); }

	const char* get_name() const override
	{ return "InspectorQuery"; }

	static EventType get_static_type()
	{ return EventType::InspectorQuery; }
};


struct ResizeEvent : public Event
{
	EventType get_event_type() const override
	{ return get_static_type(); }

	const char* get_name() const override
	{ return "ResizeEvent"; }

	static EventType get_static_type()
	{ return EventType::Resize; }
};

} // hpr
