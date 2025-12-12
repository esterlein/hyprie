#pragma once

#include <cstdint>
#include <string_view>

#include "mtp_memory.hpp"

#include "asset.hpp"
#include "handle_store.hpp"


namespace hpr::res {


template<typename T>
class AssetBank
{
public:

	AssetBank() = default;

	T* find(Handle<T> handle)
	{
		return m_store.get(handle);
	}

	const T* find(Handle<T> handle) const
	{
		return m_store.get(handle);
	}

	Asset<T>* find(std::string_view uri)
	{
		for (auto& asset : m_assets)
			if (asset.uri == uri)
				return &asset;
		return nullptr;
	}

	Asset<T>* find(uint64_t uri_hash)
	{
		for (auto& asset : m_assets)
			if (asset.uri_hash == uri_hash)
				return &asset;
		return nullptr;
	}

	const Asset<T>* find(std::string_view uri) const
	{
		for (auto& asset : m_assets)
			if (asset.uri == uri)
				return &asset;
		return nullptr;
	}

	const Asset<T>* find(uint64_t uri_hash) const
	{
		for (auto& asset : m_assets)
			if (asset.uri_hash == uri_hash)
				return &asset;
		return nullptr;
	}

	Handle<T> get_handle(std::string_view uri) const
	{
		if (const auto* asset = find(uri))
			return asset->handle;
		return Handle<T>::null();
	}

	Handle<T> get_handle(uint64_t uri_hash) const
	{
		if (const auto* asset = find(uri_hash))
			return asset->handle;
		return Handle<T>::null();
	}

	template<typename... Types>
	Asset<T>& add(std::string_view uri, Types&&... args)
	{
		const uint64_t uri_hash = hash_uri(uri);

		if (auto* existing = find(uri))
			return *existing;

		Handle<T> handle = m_store.create(std::forward<Types>(args)...);
		return m_assets.emplace_back(Asset<T> {std::string(uri), uri_hash, handle});
	}

	bool remove(std::string_view uri)
	{
		for (uint32_t i = 0; i < m_assets.size(); ++i) {
			Asset<T>& asset = m_assets[i];
			if (asset.uri == uri) {
				m_store.destroy(asset.handle);
				m_assets.erase(i);
				return true;
			}
		}
		return false;
	}

	bool remove(uint64_t uri_hash)
	{
		for (uint32_t i = 0; i < m_assets.size(); ++i) {
			Asset<T>& asset = m_assets[i];
			if (asset.uri_hash == uri_hash) {
				m_store.destroy(asset.handle);
				m_assets.erase(i);
				return true;
			}
		}
		return false;
	}

	Asset<T>* find_composite(std::string_view uri, uint32_t id)
	{
		const uint64_t composite_hash = composite_hash_uri(uri, id);
		return find(composite_hash);
	}

	const Asset<T>* find_composite(std::string_view uri, uint32_t id) const
	{
		const uint64_t composite_hash = composite_hash_uri(uri, id);
		return find(composite_hash);
	}

	template<typename... Types>
	Asset<T>& add_composite(std::string_view uri, uint32_t id, Types&&... args)
	{
		const uint64_t composite_hash = composite_hash_uri(uri, id);

		if (auto* existing = find(composite_hash))
			if (existing->uri == uri)
				return *existing;

		Handle<T> handle = m_store.create(std::forward<Types>(args)...);
		return m_assets.emplace_back(Asset<T>{std::string(uri), composite_hash, handle});
	}

	bool remove_composite(std::string_view uri, uint32_t id)
	{
		const uint64_t composite_hash = composite_hash_uri(uri, id);
		for (uint32_t i = 0; i < m_assets.size(); ++i) {
			Asset<T>& asset = m_assets[i];
			if (asset.uri_hash == composite_hash && asset.uri == uri) {
				m_store.destroy(asset.handle);
				m_assets.erase(i);
				return true;
			}
		}
		return false;
	}

	Asset<T>* find_composite(std::string_view uri, uint32_t id_0, uint32_t id_1)
	{
		const uint64_t composite_hash = composite_hash_uri(uri, id_0, id_1);
		return find(composite_hash);
	}

	const Asset<T>* find_composite(std::string_view uri, uint32_t id_0, uint32_t id_1) const
	{
		const uint64_t composite_hash = composite_hash_uri(uri, id_0, id_1);
		return find(composite_hash);
	}

	template<typename... Types>
	Asset<T>& add_composite(std::string_view uri, uint32_t id_0, uint32_t id_1, Types&&... args)
	{
		const uint64_t composite_hash = composite_hash_uri(uri, id_0, id_1);
	
		if (auto* existing = find(composite_hash))
			if (existing->uri == uri)
				return *existing;
	
		Handle<T> handle = m_store.create(std::forward<Types>(args)...);
		return m_assets.emplace_back(Asset<T>{std::string(uri), composite_hash, handle});
	}

	bool remove_composite(std::string_view uri, uint32_t id_0, uint32_t id_1)
	{
		const uint64_t composite_hash = composite_hash_uri(uri, id_0, id_1);
		for (uint32_t i = 0; i < m_assets.size(); ++i) {
			Asset<T>& asset = m_assets[i];
			if (asset.uri_hash == composite_hash && asset.uri == uri) {
				m_store.destroy(asset.handle);
				m_assets.erase(i);
				return true;
			}
		}
		return false;
	}

	Asset<T>* find_composite(std::string_view uri_0, std::string_view uri_1)
	{
		const uint64_t composite_hash = hash_uri(uri_0) ^ (hash_uri(uri_1) * k_mix_phi64);
		return find(composite_hash);
	}

	const Asset<T>* find_composite(std::string_view uri_0, std::string_view uri_1) const
	{
		const uint64_t composite_hash = hash_uri(uri_0) ^ (hash_uri(uri_1) * k_mix_phi64);
		return find(composite_hash);
	}

	template<typename... Types>
	Asset<T>& add_composite(std::string_view uri_0, std::string_view uri_1, Types&&... args)
	{
		const uint64_t composite_hash = hash_uri(uri_0) ^ (hash_uri(uri_1) * k_mix_phi64);
		if (auto* existing = find(composite_hash)) {
			if (existing->uri == uri_0)
				return *existing;
		}
		Handle<T> handle = m_store.create(std::forward<Types>(args)...);
		return m_assets.emplace_back(Asset<T>{std::string(uri_0), composite_hash, handle});
	}

	bool remove_composite(std::string_view uri_0, std::string_view uri_1)
	{
		const uint64_t composite_hash = hash_uri(uri_0) ^ (hash_uri(uri_1) * k_mix_phi64);
		for (uint32_t i = 0; i < m_assets.size(); ++i) {
			Asset<T>& asset = m_assets[i];
			if (asset.uri_hash == composite_hash && asset.uri == uri_0) {
				m_store.destroy(asset.handle);
				m_assets.erase(i);
				return true;
			}
		}
		return false;
	}

	const mtp::vault<Asset<T>, mtp::default_set>& assets() const
	{
		return m_assets;
	}

private:

	inline static constexpr uint64_t k_fnv_offset = 1469598103934665603ULL;
	inline static constexpr uint64_t k_fnv_prime  = 1099511628211ULL;
	inline static constexpr uint64_t k_mix_phi64  = 0x9E3779B97F4A7C15ULL;
	inline static constexpr uint64_t k_mix_mm364  = 0xC2B2AE3D27D4EB4FULL;

	static constexpr uint64_t hash_uri(std::string_view uri) noexcept
	{
		uint64_t uri_hash = 1469598103934665603ULL;
		for (unsigned char character : uri)
			uri_hash = (uri_hash ^ character) * 1099511628211ULL;
		return uri_hash;
	}

	static constexpr uint64_t composite_hash_uri(std::string_view uri, uint32_t id) noexcept
	{
		const uint64_t prime = 0x9E3779B97f4A7C15ULL;
		return hash_uri(uri) ^ (uint64_t {id} * prime);
	}

	static constexpr uint64_t composite_hash_uri(std::string_view uri, uint32_t id_0, uint32_t id_1) noexcept
	{
		const uint64_t prime_0 = 0x9E3779B97F4A7C15ULL;
		const uint64_t prime_1 = 0xC2B2AE3D27D4EB4FULL;
		uint64_t uri_hash = hash_uri(uri);
		uri_hash ^= (uint64_t {id_0} * prime_0);
		uri_hash ^= (uint64_t {id_1} * prime_1);
		return uri_hash;
	}

	mtp::vault<Asset<T>, mtp::default_set> m_assets {32};
	HandleStore<T> m_store {32};
};

} // hpr::res

