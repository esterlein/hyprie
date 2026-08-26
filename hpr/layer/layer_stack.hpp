#pragma once

#include "hprint.hpp"
#include "mtp_memory.hpp"

#include "layer.hpp"
#include "entity.hpp"
#include "action.hpp"
#include "surface.hpp"
#include "renderer.hpp"

#include "event_queue.hpp"
#include "input_binding.hpp"
#include "event_emitter.hpp"
#include "command_stream.hpp"
#include "command_emitter.hpp"

#include "scene_context.hpp"
#include "systems_scene.hpp"
#include "camera_controller.hpp"
#include "ecs_registry_types.hpp"

#include <memory>
#include <algorithm>


namespace hpr::lyr {


namespace cfg {

static constexpr uint32_t cmd_stream_capacity = 256U * 1024U;

} // hpr::lyr::cfg


class SceneLayer;


class LayerStack
{
public:

	LayerStack(MainRegistry& registry, rdr::SurfaceInfo surface_info)
		: m_registry     {registry}
		, m_surface_info {surface_info}
	{
		m_cmd_stream.set_storage(m_cmd_buffer.data(), cfg::cmd_stream_capacity);
	}

	void init()
	{
		m_active_cam = ecs::CameraSystem::find_active_camera(m_registry);
		HPR_ASSERT(m_active_cam != ecs::ctx::invalid_entity);

		const bool is_cam_ok = ecs::CameraSystem::init_camera_controller(
			m_registry,
			m_active_cam,
			m_cam_controller
		);
		HPR_ASSERT(is_cam_ok);
	}

	void push_layer(std::unique_ptr<Layer> layer)
	{
		if (!layer)
			return;

		if (EventEmitter* event_interface = dynamic_cast<EventEmitter*>(layer.get())) {
			event_interface->set_event_queue(m_event_queue);
		}

		layer->on_attach();
		m_layers.emplace(m_layers.begin() + m_insert_index, std::move(layer));
		++m_insert_index;
	}

	void push_overlay(std::unique_ptr<Layer> overlay)
	{
		if (!overlay)
			return;

		if (EventEmitter* event_interface = dynamic_cast<EventEmitter*>(overlay.get())) {
			event_interface->set_event_queue(m_event_queue);
		}

		if (CommandEmitter* cmd_interface = dynamic_cast<CommandEmitter*>(overlay.get())) {
			cmd_interface->set_command_stream(m_cmd_stream);
		}

		overlay->on_attach();
		m_layers.emplace_back(std::move(overlay));
	}

	void pop_layer(Layer* layer)
	{
		if (!layer)
			return;

		auto it_beg = m_layers.begin();
		auto it_end = m_layers.begin() + static_cast<std::ptrdiff_t>(m_insert_index);

		auto it = std::find_if(
			it_beg,
			it_end,
			[layer](const std::unique_ptr<Layer>& layer_stored)
			{
				return layer_stored.get() == layer;
			}
		);

		if (it != it_end) {
			(*it)->on_detach();
			m_layers.erase(it);
			--m_insert_index;
		}
	}

	void pop_overlay(Layer* overlay)
	{
		if (!overlay)
			return;

		auto it_begin = m_layers.begin() + static_cast<std::ptrdiff_t>(m_insert_index);
		auto it_end   = m_layers.end();

		auto it = std::find_if(
			it_begin,
			it_end,
			[overlay](const std::unique_ptr<Layer>& overlay_stored)
			{
				return overlay_stored.get() == overlay;
			}
		);

		if (it != it_end) {
			(*it)->on_detach();
			m_layers.erase(it);
		}
	}

	template<typename T, typename... Types>
	T* enqueue_event(Types&&... args)
	{
		static_assert(std::is_base_of_v<Event, T>);

		return m_event_queue.push<T>(std::forward<Types>(args)...);
	}

	bool on_event(Event& event)
	{
		for (std::ptrdiff_t i = static_cast<std::ptrdiff_t>(m_layers.size()) - 1; i >= 0; --i) {
			m_layers[static_cast<size_t>(i)]->on_event(event); 
		}
		return event.handled;
	}

	bool on_actions(std::span<const io::Action> actions)
	{
		bool action_consumed = false;

		for (const io::Action& action : actions) {
			switch (action.kind) {
			case io::ActionKind::Orbit: {
				const auto& payload = std::get<io::OrbitAction>(action.payload);
				m_cam_controller.delta.orbit_x += payload.delta_x;
				m_cam_controller.delta.orbit_y += payload.delta_y;
				action_consumed = true;
			}
			break;
			case io::ActionKind::Pan: {
				const auto& payload = std::get<io::PanAction>(action.payload);
				m_cam_controller.delta.pan_x += payload.delta_x;
				m_cam_controller.delta.pan_y += payload.delta_y;
				action_consumed = true;
			}
			break;
			case io::ActionKind::Dolly: {
				const auto& payload = std::get<io::DollyAction>(action.payload);
				m_cam_controller.delta.dolly += payload.amount;
				action_consumed = true;
			}
			break;
			case io::ActionKind::Move: {
				const auto& payload = std::get<io::MoveAction>(action.payload);
				m_cam_controller.delta.move_forward = payload.forward;
				m_cam_controller.delta.move_right   = payload.right;
				m_cam_controller.delta.move_up      = payload.up;
				action_consumed = true;
			}
			break;

			case io::ActionKind::CameraModeToggle: {
				m_cam_controller.mode =
					m_cam_controller.mode == scn::CameraController::Mode::iso
					? scn::CameraController::Mode::fly
					: scn::CameraController::Mode::iso;
				action_consumed = true;
			}
			break;
			default: break;
			}
		}

		for (std::ptrdiff_t i = static_cast<std::ptrdiff_t>(m_layers.size()) - 1; i >= 0; --i) {
			if (m_layers[static_cast<size_t>(i)]->on_actions(m_scene_ctx, actions))
				return true;
		}
		return action_consumed;
	}

	void on_update(rdr::Renderer& renderer, float delta_time)
	{
		m_scene_ctx = {};

		ecs::CameraSystem::update_camera_controller(
			m_registry,
			m_active_cam,
			m_cam_controller,
			delta_time,
			m_binding.pan_sensitivity,
			m_binding.dolly_sensitivity
		);
	
		ecs::HierarchySystem::update(m_registry);
		ecs::TransformSystem::update(m_registry);
	
		m_scene_ctx.draw_view = ecs::CameraSystem::build_view(
			m_registry,
			m_active_cam,
			m_cam_controller,
			m_surface_info.aspect
		);

		for (std::ptrdiff_t i = static_cast<std::ptrdiff_t>(m_layers.size()) - 1; i >= 0; --i) {
			m_layers[static_cast<size_t>(i)]->on_update(m_scene_ctx, delta_time);
		}

		process_commands();
		process_events();

		apply_pending_transitions();

		renderer.set_scene_context(m_scene_ctx);
	}

	void on_submit()
	{
		for (size_t index = 0; index < m_layers.size(); ++index) {
			m_layers[index]->on_submit(m_scene_ctx, static_cast<uint32_t>(index));
		}
	}

private:

	void process_commands()
	{
		if (m_insert_index == 0)
			return;

		Layer* scene = m_layers[m_insert_index - 1].get();
		if (!scene)
			return;

		CmdStream::Reader reader;
		reader.begin(m_cmd_stream);
		scene->process_commands(reader);

		m_cmd_stream.reset();
	}


	void process_events()
	{
		m_event_queue.freeze();

		auto& queue = m_event_queue.queue();
		for (size_t event_index = 0; event_index < queue.size(); ++event_index) {

			std::unique_ptr<Event>& event_item = queue[event_index];

			Event& event = *event_item;
			for (std::ptrdiff_t layer_idx = static_cast<std::ptrdiff_t>(m_layers.size()) - 1; layer_idx >= 0; --layer_idx) {
				m_layers[static_cast<std::size_t>(layer_idx)]->on_event(event);
				if (event.handled)
					break;
			}
		}

		m_event_queue.clear();
	}

	void apply_pending_transitions()
	{
		for (size_t i = 0; i < m_layers.size(); ++i) {
			std::unique_ptr<Layer> next = m_layers[i]->take_transition();
			if (next) {
				m_layers[i]->on_detach();
				next->on_attach();
				m_layers[i] = std::move(next);
			}
		}
	}

private:

	MainRegistry&         m_registry;
	ecs::Entity           m_active_cam;
	scn::CameraController m_cam_controller {};
	scn::SceneContext     m_scene_ctx      {};
	rdr::SurfaceInfo      m_surface_info   {};

	mtp::vault<std::unique_ptr<Layer>, mtp::default_set> m_layers;

	size_t m_insert_index {0};

	io::InputBinding  m_binding;

	EventQueue m_event_queue;
	CmdStream  m_cmd_stream;

	alignas(8) std::array<uint8_t, cfg::cmd_stream_capacity> m_cmd_buffer {};
};

} // hpr::lyr

