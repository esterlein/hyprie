#pragma once

#include <memory>
#include <algorithm>

#include "mtp_memory.hpp"

#include "layer.hpp"
#include "event_queue.hpp"
#include "event_emitter.hpp"
#include "command_emitter.hpp"
#include "command_stream.hpp"


namespace hpr {


class SceneLayer;


class LayerStack
{
public:

	LayerStack()
	{
		m_cmd_stream.set_storage(m_cmd_buffer.data(), k_cmd_stream_capacity);
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

		auto it_begin = m_layers.begin();
		auto it_end = m_layers.begin() + static_cast<std::ptrdiff_t>(m_insert_index);
		auto it = std::find_if(it_begin, it_end, [layer](const std::unique_ptr<Layer>& layer_stored)
		{
			return layer_stored.get() == layer;
		});
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
		auto it_end = m_layers.end();
		auto it = std::find_if(it_begin, it_end, [overlay](const std::unique_ptr<Layer>& overlay_stored)
		{
			return overlay_stored.get() == overlay;
		});
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

	bool on_event(const sapp_event* event)
	{
		if (!event)
			return false;

		if (event->type == SAPP_EVENTTYPE_RESIZED) {
			m_event_queue.push<ResizeEvent>();
		}

		return false;
	}

	bool on_event(Event& event)
	{
		for (std::ptrdiff_t i = static_cast<std::ptrdiff_t>(m_layers.size()) - 1; i >= 0; --i) {
			m_layers[static_cast<size_t>(i)]->on_event(event); 

			if (event.handled)
				return true;
		}
		return false;
	}

	bool on_actions(std::span<const Action> actions)
	{
		for (std::ptrdiff_t i = static_cast<std::ptrdiff_t>(m_layers.size()) - 1; i >= 0; --i) {
			if (m_layers[static_cast<size_t>(i)]->on_actions(actions))
				return true;
		}
		return false;
	}

	void on_update(float delta_time)
	{
		for (std::ptrdiff_t i = static_cast<std::ptrdiff_t>(m_layers.size()) - 1; i >= 0; --i) {
			m_layers[static_cast<size_t>(i)]->on_update(delta_time);
		}

		process_commands();
		process_events();

		apply_pending_transitions();
	}

	void on_submit(rdr::Renderer& renderer)
	{
		for (size_t index = 0; index < m_layers.size(); ++index) {
			m_layers[index]->on_submit(renderer, static_cast<uint32_t>(index));
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
			for (std::ptrdiff_t layer_index = static_cast<std::ptrdiff_t>(m_layers.size()) - 1; layer_index >= 0; --layer_index) {
				m_layers[static_cast<std::size_t>(layer_index)]->on_event(event);
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

	//TODO: replace with a proper data structure

	mtp::vault<std::unique_ptr<Layer>, mtp::default_set> m_layers;

	size_t m_insert_index {0};

	EventQueue m_event_queue;
	CmdStream  m_cmd_stream;

	static constexpr uint32_t k_cmd_stream_capacity = 256U * 1024U;
	alignas(8) std::array<uint8_t, k_cmd_stream_capacity> m_cmd_buffer {};
};

} // hpr

