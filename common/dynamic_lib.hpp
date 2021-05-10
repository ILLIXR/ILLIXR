#pragma once

#include <dlfcn.h>
#include <iostream>
#include <memory>
#include <string>
#include <functional>

#include "global_module_defs.hpp"
#include "error_util.hpp"

namespace ILLIXR {

using void_ptr = std::unique_ptr<void, std::function<void(void*)>>;
/*
Usage:
    void* thing;
    void_ptr wrapped_thing = {thing, [](void* thing) {
        // destructor goes here.
    }}
    // wrapped_thing.get() returns underlying thing.
 */

class dynamic_lib {
private:
	dynamic_lib(void_ptr&& handle)
		: _m_handle{std::move(handle)}
	{ }

public:
	dynamic_lib(dynamic_lib&& other)
		: _m_handle{std::move(other._m_handle)}
	{ }

	dynamic_lib& operator=(dynamic_lib&& other) {
		if (this != &other) {
			_m_handle = std::move(other._m_handle);
		}
		return *this;
	}

	static dynamic_lib create(const std::string& path) {
		return dynamic_lib::create(std::string_view{path.c_str()});
	}

	static dynamic_lib create(const std::string_view& path) {
		char* error;

		// dlopen man page says that it can set errno sp
		RAC_ERRNO_MSG("dynamic_lib before dlopen");
		void* handle = dlopen(path.data(), RTLD_LAZY | RTLD_LOCAL);
		RAC_ERRNO_MSG("dynamic_lib after dlopen");

		if ((error = dlerror()) || !handle) {
			throw std::runtime_error{
				"dlopen(\"" + std::string{path} + "\"): " + (error == nullptr ? "NULL" : std::string{error})};
        }

		return dynamic_lib{void_ptr{handle, [](void* handle) {
            RAC_ERRNO();
			
			char* error;
			int ret = dlclose(handle);
			if ((error = dlerror()) || ret)
				throw std::runtime_error{
					"dlclose(): " + (error == nullptr ? "NULL" : std::string{error})};
		}}};
	}

	const void* operator[](const std::string& symbol_name) const {
		RAC_ERRNO_MSG("dynamic_lib at start of operator[]");

		char* error;		
		void* symbol = dlsym(_m_handle.get(), symbol_name.c_str());
		if ((error = dlerror()))
			throw std::runtime_error{
				"dlsym(\"" + symbol_name + "\"): " + (error == nullptr ? "NULL" : std::string{error})};
		return symbol;
	}

	template <typename T>
	const T get(const std::string& symbol_name) const {
		const void* obj = (*this)[symbol_name];
		// return reinterpret_cast<const T>((*this)[symbol_name]);
		return (const T) obj;
	}

private:
	void_ptr _m_handle;
};

}
