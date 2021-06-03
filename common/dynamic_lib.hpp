#pragma once

#include <dlfcn.h>
#include <iostream>
#include <memory>
#include <string>
#include <string_view>
#include <functional>
#include <algorithm>

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
    dynamic_lib(
        void_ptr&& handle
        , const std::string& lib_path = ""
    ) : _m_handle{std::move(handle)}
      , _m_lib_path{std::move(lib_path)}
    { }

public:
    dynamic_lib(dynamic_lib&& other)
        : _m_handle{std::move(other._m_handle)}
        , _m_lib_path{std::move(other._m_lib_path)}
    { }

    dynamic_lib& operator=(dynamic_lib&& other) {
        if (this != &other) {
            _m_handle   = std::move(other._m_handle);
            _m_lib_path = std::move(other._m_lib_path);
        }
        return *this;
    }

    ~dynamic_lib() {
#ifndef NDEBUG
        if (!_m_lib_path.empty()) {
            std::cout << "[dynamic_lib] Destructing library : " << _m_lib_path << std::endl;
        }
#endif /// NDEBUG
    }

    static dynamic_lib create(const std::string& path) {
        return dynamic_lib::create(std::string_view{path.c_str()});
    }

    static dynamic_lib create(const std::string_view& path) {
        char* error;

        /// The contents of a string_view must not outlive the parent string
        /// Copy the contents passed to create into a new string
        std::vector<char> path_buf;
        path_buf.resize(path.size() + 1U);
        std::copy(path.cbegin(), path.cend(), path_buf.begin());
        path_buf.back() = '\0';

        std::string path_copy {path_buf.cbegin(), path_buf.cend()};
#ifndef NDEBUG
        std::cout << "[dynamic_lib] Opening library : " << path_copy << std::endl;
#endif /// NDEBUG

        // dlopen man page says that it can set errno sp
        RAC_ERRNO_MSG("dynamic_lib before dlopen");
        void* handle = dlopen(path.data(), RTLD_LAZY | RTLD_LOCAL);
        RAC_ERRNO_MSG("dynamic_lib after dlopen");

        if ((error = dlerror()) || !handle) {
            throw std::runtime_error{
                "dlopen(\"" + std::string{path} + "\"): " + (error == nullptr ? "NULL" : std::string{error})
            };
        }

        return dynamic_lib{
            void_ptr{handle, [](void* handle) {
                RAC_ERRNO();

                char* error;
                int ret = dlclose(handle);
                if ((error = dlerror()) || ret) {
                    const std::string msg_error {"dlclose(): " + (error == nullptr ? "NULL" : std::string{error})};
#ifndef NDEBUG
                    /// If debugging, only report the dlclose error (non-fatal, can leak memory)
                    std::cerr << "[dynamic_lib] " << msg_error << std::endl;
#else
                    /// If not debugging, raise the dlclose error (fatal)
                    throw std::runtime_error{msg_error};
#endif /// NDEBUG
                }
            }},
            path_copy /// Keep the dynamic lib name for debugging
        };
    }

    const void* operator[](const std::string& symbol_name) const {
        RAC_ERRNO_MSG("dynamic_lib at start of operator[]");

        char* error;
        void* symbol = dlsym(_m_handle.get(), symbol_name.c_str());
        if ((error = dlerror())) {
            throw std::runtime_error{
                "dlsym(\"" + symbol_name + "\"): " + (error == nullptr ? "NULL" : std::string{error})
            };
        }
        return symbol;
    }

    template <typename T>
    const T get(const std::string& symbol_name) const {
        const void* obj = (*this)[symbol_name];
        // return reinterpret_cast<const T>((*this)[symbol_name]);
        return (const T) obj;
    }

private:
    void_ptr    _m_handle;
    std::string _m_lib_path;
};

}
