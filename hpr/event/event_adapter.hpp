#pragma once

#include "event.hpp"
#include "surface.hpp"

#include "sokol_app.h"

#include <memory>


namespace hpr {


class EventAdapter
{
public:

	static std::unique_ptr<Event> adapt(const sapp_event* app_event)
	{
		if (!app_event)
			return nullptr;

		switch (app_event->type) {
		case SAPP_EVENTTYPE_RESIZED: {
			auto resize_event = std::make_unique<ResizeEvent>();
			resize_event->surface_info = rdr::query_surface_info();
			return resize_event;
		}

		default:
			return nullptr;
		}
	}
};

} // hpr
