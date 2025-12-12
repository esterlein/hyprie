#pragma once

#include <tuple>
#include <cassert>
#include <type_traits>

#include "handle.hpp"
#include "asset_bank.hpp"
#include "asset_data.hpp"


namespace hpr::res {


template<typename T, typename Storage>
struct ResolverEntry
{
	using payload = T;
	using storage = Storage;
};


template<typename... StorageEntries>
class HandleResolver
{
private:

	template<typename T, typename Entry>
	struct entry_for : std::is_same<T, typename Entry::payload>
	{};

	template<typename T, typename... Es>
	struct contains : std::bool_constant<(entry_for<T, Es>::value || ...)>
	{};

	template<typename T, typename... Es>
	struct type_index
	{};

	template<typename T, typename First, typename... Rest>
	struct type_index<T, First, Rest...> : std::integral_constant<std::size_t,
		entry_for<T, First>::value ? 0 : 1 + type_index<T, Rest...>::value>
	{};

	template<typename T, typename Last>
	struct type_index<T, Last> : std::integral_constant<std::size_t,
		entry_for<T, Last>::value ? 0 : static_cast<std::size_t>(-1)>
	{};

	template<typename T>
	static constexpr std::size_t index_of = type_index<T, StorageEntries...>::value;

	template<typename Storage, typename T, typename = void>
	struct has_find : std::false_type
	{};

	template<typename Storage, typename T>
	struct has_find<Storage, T, std::void_t<decltype(std::declval<const Storage&>().find(std::declval<Handle<T>>()))>> : std::true_type
	{};

	template<typename Storage, typename T, typename = void>
	struct has_get : std::false_type
	{};

	template<typename Storage, typename T>
	struct has_get<Storage, T, std::void_t<decltype(std::declval<const Storage&>().get(std::declval<Handle<T>>()))>> : std::true_type
	{};


	template<typename Storage, typename T>
	static inline const T* storage_lookup(const Storage* storage, Handle<T> handle)
	{
		if constexpr (has_find<Storage, T>::value) {
			return storage->find(handle);
		}
		else if constexpr (has_get<Storage, T>::value) {
			return storage->get(handle);
		}
		else {
			static_assert(!sizeof(Storage),
				"storage has no access methods for this handle type");

			return nullptr;
		}
	}

public:

	template<typename T>
	using storage_t = typename std::tuple_element<index_of<T>, std::tuple<typename StorageEntries::storage...>>::type;


	HandleResolver() = default;


	HandleResolver(typename StorageEntries::storage&... storages)
	{
		(this->template set_storage<typename StorageEntries::payload>(storages), ...);
	}


	template<typename T>
	void set_storage(storage_t<T>& storage)
	{
		static_assert(contains<T, StorageEntries...>::value,
			"wrong type for resolver");
		static_assert(index_of<T> != static_cast<std::size_t>(-1),
			"internal index lookup failed");

		std::get<index_of<T>>(m_storages) = &storage;
	}

	template<typename T>
	requires (!std::is_const_v<storage_t<T>>)
	T* resolve(Handle<T> handle)
	{
		static_assert(contains<T, StorageEntries...>::value,
			"wrong type for resolver");
		static_assert(index_of<T> != static_cast<std::size_t>(-1),
			"internal index lookup fail");

		const auto* storage = std::get<index_of<T>>(m_storages);
		assert(storage);
		return storage ? const_cast<T*>(storage_lookup(storage, handle)) : nullptr;
	}

	template<typename T>
	const T* resolve(Handle<T> handle) const
	{
		static_assert(contains<T, StorageEntries...>::value,
			"wrong type for resolver");
		static_assert(index_of<T> != static_cast<std::size_t>(-1),
			"internal index lookup fail");

		const auto* storage = std::get<index_of<T>>(m_storages);
		assert(storage);
		return storage ? storage_lookup(storage, handle) : nullptr;
	}

private:

	std::tuple<typename StorageEntries::storage*...> m_storages {};
};

} // hpr::res

