#pragma once

#include <string>
#include "handle.hpp"


namespace hpr {


template<typename T>
struct Asset
{
	std::string uri;
	uint64_t    uri_hash {0};
	Handle<T>   handle   {0};
};

} // hpr
