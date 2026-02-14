#include <type_traits>

#include "mtp_memory.hpp"
#include "nuklear_cfg.hpp"


template <typename MtpSet>
struct MetapoolNuklearAllocator
{
	using Metapool = std::remove_reference_t<decltype(mtp::get_tls_allocator<MtpSet>())>;

	static void* alloc_fn(nk_handle handle, void*, nk_size size)
	{
		auto* metapool = static_cast<Metapool*>(handle.ptr);
		if (size == 0)
			return nullptr;
		return metapool->alloc(size, alignof(std::max_align_t));
	}

	static void free_fn(nk_handle handle, void* block)
	{
		auto* metapool = static_cast<Metapool*>(handle.ptr);
		metapool->free(static_cast<std::byte*>(block));
	}

	static nk_allocator make()
	{
		nk_allocator nk_metapool {};
		nk_metapool.userdata.ptr = mtp::get_tls_allocator<MtpSet>(mtp::as_ptr);
		nk_metapool.alloc        = &MetapoolNuklearAllocator<MtpSet>::alloc_fn;
		nk_metapool.free         = &MetapoolNuklearAllocator<MtpSet>::free_fn;
		return nk_metapool;
	}
};

