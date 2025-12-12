#pragma once

#include <tuple>
#include <utility>
#include <type_traits>

#include "mtp_memory.hpp"

#include "handle.hpp"
#include "entity.hpp"


namespace hpr::ecs {


inline constexpr uint32_t k_invalid_slot {0xFFFFFFFFU};


template <typename... Components>
class Registry
{
public:

	Registry()  = default;
	~Registry() = default;


	Entity create_entity()
	{
		if (!m_recycled.empty()) {
			Entity index = m_recycled.back();
			m_recycled.pop_back();
			m_alive[index] = 1U;
			++m_live_count;
			return index;
		}

		Entity index = m_next_entity++;

		if (index >= m_generation.size()) {
			m_generation.resize(static_cast<size_t>(index) + 1, 1U);
			m_alive.resize(static_cast<size_t>(index) + 1, 1U);
			for_each_rack([&index](auto& store) {
				store.ensure_sparse_capacity(static_cast<size_t>(index) + 1);
			});
		}
		else {
			m_alive[index] = 1U;
		}
		++m_live_count;
		return index;
	}


	Handle<Entity> create_handle()
	{
		Entity index = create_entity();
		return { index, m_generation[index] };
	}


	void destroy_entity(Entity entity)
	{
		if (entity >= m_generation.size())
			return;

		if (!m_alive[entity])
			return;

		for_each_rack([&entity](auto& store) {
			store.erase_entity(entity);
		});

		m_alive[entity] = 0U;
		++m_generation[entity];
		m_recycled.push_back(entity);

		if (m_live_count > 0)
			--m_live_count;
	}


	void destroy_entity(Handle<Entity> handle)
	{
		if (!is_valid(handle))
			return;
		destroy_entity(handle.index);
	}


	template <typename T>
	bool has(Entity entity) const
	{
		const auto& rack = get_rack_const<T>();

		if (entity >= rack.sparse_index.size())
			return false;

		return rack.sparse_index[entity] != k_invalid_slot;
	}


	template <typename T>
	bool has(Handle<Entity> handle) const
	{
		if (!is_valid(handle))
			return false;

		return has<T>(handle.index);
	}


	template <typename T, typename... Types>
	T& add(Entity entity, Types&&... args)
	{
		auto& rack = get_rack<T>();

		if (entity >= rack.sparse_index.size())
			rack.sparse_index.resize(static_cast<size_t>(entity) + 1U, k_invalid_slot);

		uint32_t slot = rack.sparse_index[entity];
		if (slot == k_invalid_slot) {
			slot = static_cast<uint32_t>(rack.dense_values.size());
			rack.sparse_index[entity] = slot;
			rack.dense_entities.emplace_back(entity);
			rack.dense_values.emplace_back(std::forward<Types>(args)...);
			return rack.dense_values[slot];
		}

		rack.dense_values[slot] = T(std::forward<Types>(args)...);
		return rack.dense_values[slot];
	}


	template <typename T, typename... Types>
	T& add(Handle<Entity> handle, Types&&... args)
	{
		if (!is_valid(handle))
			return add<T>(create_entity(), std::forward<Types>(args)...);
		return add<T>(handle.index, std::forward<Types>(args)...);
	}


	template <typename T>
	void remove(Entity entity)
	{
		auto& rack = get_rack<T>();

		if (entity >= rack.sparse_index.size())
			return;

		std::uint32_t slot = rack.sparse_index[entity];
		if (slot == k_invalid_slot)
			return;

		std::uint32_t last = static_cast<std::uint32_t>(rack.dense_values.size() - 1U);

		if (slot != last) {
			rack.dense_values[slot] = std::move(rack.dense_values[last]);
			rack.dense_entities[slot] = rack.dense_entities[last];
			rack.sparse_index[rack.dense_entities[slot]] = slot;
		}

		rack.dense_values.resize(last);
		rack.dense_entities.resize(last);
		rack.sparse_index[entity] = k_invalid_slot;
	}


	template <typename T>
	void remove(Handle<Entity> handle)
	{
		if (!is_valid(handle))
			return;

		remove<T>(handle.index);
	}


	template <typename T>
	T* get(Entity entity)
	{
		auto& rack = get_rack<T>();

		if (entity >= rack.sparse_index.size())
			return nullptr;

		std::uint32_t slot = rack.sparse_index[entity];
		if (slot == k_invalid_slot)
			return nullptr;

		return &rack.dense_values[slot];
	}


	template <typename T>
	T* get(Handle<Entity> handle)
	{
		if (!is_valid(handle))
			return nullptr;
		return get<T>(handle.index);
	}


	template <typename T>
	const T* get(Entity entity) const
	{
		const auto& rack = get_rack_const<T>();

		if (entity >= rack.sparse_index.size())
			return nullptr;

		std::uint32_t slot = rack.sparse_index[entity];
		if (slot == k_invalid_slot)
			return nullptr;

		return &rack.dense_values[slot];
	}


	template <typename T>
	const T* get(Handle<Entity> handle) const
	{
		if (!is_valid(handle))
			return nullptr;

		return get<T>(handle.index);
	}


	template <typename T, typename Func>
	void each(Func&& func)
	{
		auto& rack = get_rack<T>();
		const std::size_t count = rack.dense_values.size();

		for (std::size_t i = 0; i < count; ++i) {
		    func(rack.dense_entities[i], rack.dense_values[i]);
		}
	}


	template <typename Primary, typename... Secondary, typename Func>
	void scan(Func&& func)
	{
		auto& primary_rack = get_rack<Primary>();
		const std::size_t count = primary_rack.dense_values.size();
		for (std::size_t i = 0; i < count; ++i) {
			Entity entity = primary_rack.dense_entities[i];
			if (!(has<Secondary>(entity) && ...))
				continue;

			func(entity, primary_rack.dense_values[i], *get<Secondary>(entity)...);
		}
	}


	template <typename T>
	void clear_rack()
	{
		get_rack<T>().clear();
	}


	template <typename T>
	std::size_t size() const
	{
		return get_rack_const<T>().dense_values.size();
	}

	template <typename T>
	std::size_t size_entities() const
	{
		return get_rack_const<T>().dense_entities.size();
	}

	template <typename T>
	std::size_t size_values() const
	{
		return get_rack_const<T>().dense_values.size();
	}

	template <typename T>
	std::size_t size_index() const
	{
		return get_rack_const<T>().sparse_index.size();
	}

	template <typename T>
	std::size_t capacity() const
	{
		return get_rack_const<T>().sparse_index.size();
	}


	void reserve_entities(std::size_t capacity)
	{
		if (capacity > m_generation.size()) {
			m_generation.resize(capacity, 1U);
			m_alive.resize(capacity, 0U);
			for_each_rack([&capacity](auto& store) {
				store.ensure_sparse_capacity(capacity);
			});
		}
	}


	bool alive(Entity entity) const
	{
		if (entity >= m_generation.size())
			return false;
		return m_alive[entity] != 0U;
	}


	bool is_valid(Handle<Entity> handle) const
	{
		if (handle.index >= m_generation.size())
			return false;
		if (m_generation[handle.index] != handle.magic)
			return false;
		return m_alive[handle.index] != 0U;
	}


	void clear()
	{
		for_each_rack([](auto& store) {
			store.clear();
		});

		m_recycled.resize(0);
		m_generation.resize(0);
		m_alive.resize(0);

		m_next_entity = 0;
		m_live_count = 0;
	}


	std::size_t entity_count() const
	{
		return m_live_count;
	}

private:

	template <typename T>
	struct Rack
	{
		mtp::vault<Entity, mtp::default_set>        dense_entities;
		mtp::vault<T, mtp::default_set>             dense_values;
		mtp::vault<std::uint32_t, mtp::default_set> sparse_index;

		void ensure_sparse_capacity(std::size_t cap)
		{
			if (cap > sparse_index.size())
				sparse_index.resize(cap, k_invalid_slot);
		}

		void erase_entity(Entity entity)
		{
			if (entity >= sparse_index.size())
				return;

			std::uint32_t slot = sparse_index[entity];
			if (slot == k_invalid_slot)
				return;

			std::uint32_t last = static_cast<std::uint32_t>(dense_values.size() - 1U);
			if (slot != last) {
				dense_values[slot] = std::move(dense_values[last]);
				dense_entities[slot] = dense_entities[last];
				sparse_index[dense_entities[slot]] = slot;
			}

			dense_values.resize(last);
			dense_entities.resize(last);
			sparse_index[entity] = k_invalid_slot;
		}

		void clear()
		{
			dense_entities.resize(0);
			dense_values.resize(0);
			sparse_index.resize(0, k_invalid_slot);
		}
	};


	template <typename T>
	static constexpr bool is_registered_v = (std::is_same_v<T, Components> || ...);


	template <typename T>
	Rack<T>& get_rack()
	{
		static_assert(is_registered_v<T>, "[get_rack] component T is not registered");
		return std::get<Rack<T>>(m_racks);
	}


	template <typename T>
	const Rack<T>& get_rack_const() const
	{
		static_assert(is_registered_v<T>, "[get_rack] component T is not registered");
		return std::get<Rack<T>>(m_racks);
	}


	template <typename Func>
	void for_each_rack(Func&& func)
	{
		for_each_rack_implementation(std::forward<Func>(func), std::make_index_sequence<sizeof...(Components)>{});
	}


	template <typename Func, std::size_t... Is>
	void for_each_rack_implementation(Func&& func, std::index_sequence<Is...>)
	{
		(func(std::get<Is>(m_racks)), ...);
	}

private:

	std::tuple<Rack<Components>...> m_racks;

	mtp::vault<Entity,        mtp::default_set> m_recycled;
	mtp::vault<std::uint32_t, mtp::default_set> m_generation;
	mtp::vault<std::uint8_t,  mtp::default_set> m_alive;

	Entity m_next_entity {0};
	size_t m_live_count  {0};
};

} // hpr::ecs

