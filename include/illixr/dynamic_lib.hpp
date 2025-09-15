#pragma once

#include "error_util.hpp"

#if defined(_WIN32) || defined(_WIN64)
#include <Windows.h>
#else
#include <dlfcn.h>
#endif
#include <functional>
#include <iostream>
#include <memory>
#include <spdlog/spdlog.h>
#include <string>
#include <utility>

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
public:
    dynamic_lib(dynamic_lib&& other) noexcept
        : handle_{std::move(other.handle_)}
        , library_path_{std::move(other.library_path_)} { }

    dynamic_lib& operator=(dynamic_lib&& other) noexcept {
        if (this != &other) {
            handle_       = std::move(other.handle_);
            library_path_ = std::move(other.library_path_);
        }
        return *this;
    }

    ~dynamic_lib() {
#ifndef NDEBUG
        if (!library_path_.empty()) {
            spdlog::get("illixr")->debug("[dynamic_lib] Destructing library : {}", library_path_);
        }
#endif /// NDEBUG
    }

    static dynamic_lib create(const std::string& path) {
        return dynamic_lib::create(std::string_view{path});
    }

    static dynamic_lib create(const std::string_view& path) {
        char* error;

        // dlopen man page says that it can set errno sp
        RAC_ERRNO_MSG("dynamic_lib before dlopen");
        void* handle = dlopen(path.data(), RTLD_LAZY | RTLD_LOCAL);
        RAC_ERRNO_MSG("dynamic_lib after dlopen");

        if ((error = dlerror()) || !handle) {
            spdlog::get("illixr")->error(error);
            throw std::runtime_error{"dlopen(\"" + std::string{path} +
                                     "\"): " + (error == nullptr ? "NULL" : std::string{error})};
        }

        return dynamic_lib{
            void_ptr{handle,
                     [](void* handle) {
                         RAC_ERRNO();

                         char* error;
                         int   ret = dlclose(handle);
                         if ((error = dlerror()) || ret) {
                             const std::string msg_error{"dlclose(): " + (error == nullptr ? "NULL" : std::string{error})};
                             spdlog::get("illixr")->error("[dynamic_lib] {}", msg_error);
                             throw std::runtime_error{msg_error};
                         }
                     }},
            std::string{path} /// Keep the dynamic lib name for debugging
        };
    }

    const void* operator[](const std::string& symbol_name) const {
        RAC_ERRNO_MSG("dynamic_lib at start of operator[]");

        char* error;
        void* symbol = dlsym(handle_.get(), symbol_name.c_str());
        if ((error = dlerror())) {
            throw std::runtime_error{"dlsym(\"" + symbol_name + "\"): " + std::string{error}};
        }
        return symbol;
    }

    template<typename T>
    T get(const std::string& symbol_name) const {
        const void* obj = (*this)[symbol_name];
        // return reinterpret_cast<const T>((*this)[symbol_name]);
        return (const T) obj;
    }

private:
    explicit dynamic_lib(void_ptr&& handle, std::string lib_path = "")
        : handle_{std::move(handle)}
        , library_path_{std::move(lib_path)} { }

    void_ptr    handle_;
    std::string library_path_;
};

} // namespace ILLIXR
