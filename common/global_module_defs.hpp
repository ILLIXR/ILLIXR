// Common parameters. Ultimately, these need to be moved to a yaml file.

#pragma once

#include "phonebook.hpp"
#include "relative_clock.hpp"

#include <cstdint>
#include <cstdlib>
#include <functional>
#include <iostream>
#include <math.h>
#include <stdexcept>
#include <string>
#include <string_view>

namespace ILLIXR {

/// Display parameters
struct display_params {
    // Display width in pixels
    static constexpr unsigned width_pixels = 2560;

    // Display height in pixels
    static constexpr unsigned height_pixels = 1440;

    // Display width in meters
    static constexpr float width_meters = 0.11047f;

    // Display height in meters
    static constexpr float height_meters = 0.06214f;

    // Separation between lens centers in meters
    static constexpr float lens_separation = width_meters / 2.0f;

    // Vertical position of the lens in meters
    static constexpr float lens_vertical_position = height_meters / 2.0f;

    // Display horizontal field-of-view in degrees
    static constexpr float fov_x = 90.0f;

    // Display vertical field-of-view in degrees
    static constexpr float fov_y = 90.0f;

    // Meters per tangent angle at the center of the HMD (required by timewarp_gl's distortion correction)
    static constexpr float meters_per_tan_angle = width_meters / (2 * (fov_x * M_PI / 180.0f));

    // Inter-pupilary distance (ipd) in meters
    static constexpr float ipd = 0.064f;

    // Display refresh rate in Hz
    static constexpr float frequency = 120.0f;

    // Display period in nanoseconds
    static constexpr duration period = freq2period(frequency);

    // Chromatic aberration constants
    static constexpr float aberration[4] = {-0.016f, 0.0f, 0.024f, 0.0f};
};

/// Rendering parameters
struct rendering_params {
    // Near plane distance in meters
    static constexpr float near_z = 0.1f;

    // Far plane distance in meters
    static constexpr float far_z = 20.0f;
};

// ********************************DELETE WHEN FINISHED *****************************
/**
 * @brief Convert a string containing a (python) boolean to the bool type
 */
inline bool str_to_bool(std::string var) {
    return (var == "True") ? true
        : (var == "False") ? false
                           : throw std::runtime_error("Invalid conversion from std::string to bool");
}

/// Temporary environment variable getter. Not needed once #198 is merged.
inline std::string getenv_or(std::string var, std::string default_) {
    if (std::getenv(var.c_str())) {
        return {std::getenv(var.c_str())};
    } else {
        return default_;
    }
}

// ********************************DELETE WHEN FINISHED *****************************

/*--- Declarations ---*/
/**
 * @brief (Macro expansion) Function used to create string literals from variable names
 *
 * _X: The literal text (i.e. variable name) to be stringified
 */
#ifndef STRINGIFY
    #define STRINGIFY_HELP(_X) #_X
    #define STRINGIFY(_X)      STRINGIFY_HELP(_X)
#endif // STRINGIFY

/**
 * (Macro expansion) Function used to convert to a type from a string
 *
 * _TYPE:   The type name returned by the function
 * _SUFFIX: The suffix to be applied for the std::sto.. function called
 */
#ifndef DECLARE_TO_FUNC
    #define DECLARE_TO_FUNC(_TYPE, _SUFFIX)      \
        template<>                               \
        _TYPE to(const std::string& value_str) { \
            return std::sto##_SUFFIX(value_str); \
        }
#endif // DECLARE_TO_FUNC

/**
 * @brief (Macro expansion) Function used to declare constants used by modules
 *
 * _NAME:       The constant's name to be used by module code
 * _TYPE:       The type name for the constant
 * _FROM_STR:   The conversion function from a std::string to a value of type _TYPE
 * _VALUE:      The default value for the constant
 *
 * This declaration provides two definitions given a variable name "FOO":
 *   1. A class type DECL_FOO derived from const_decl which holds const members
 *      of type _TYPE, one for the value, and a second for the default value
 *   2. An object FOO of type DECL_FOO used to perform lookups via accessors
 *
 * These declarations are designed to be placed inside a parent structure called
 * const_registry which is a phonebook service providing direct lookup access
 *
 * If a client (out-of-repo) module requires access to constant FOO, we currently
 * must also add a `#define FOO FOO` preceeding the call to DECLARE_CONST so that
 * we can detect missing definitions from within modules in a backwards compatible way
 *
 * See `configs/global_module_defs.yaml` for examples overriding default values
 *
 * Sample usage:
 * \code{.cpp}
 * #include "common/global_module_defs.hpp"
 * class my_plugin : public plugin {
 * public:
 *     my_plugin(std::string name, phonebook* pb)
 *         : plugin{name, pb}
 *         , cr{pb->lookup_impl<const_registry>()}
 *         , _m_foo{cr->FOO.value()}
 *     { }
 *     void print_foo() { std::cout << _m_foo << std::endl; }
 * private:
 *     const int _m_foo;
 * };
 * \endcode
 *
 * TODO: Support dynamic type casting
 * TODO: Support constexpr
 * TODO: Support complex, non-integral types
 * TODO: Support destructor body
 */
#ifndef DECLARE_CONST
    #define DECLARE_CONST(_NAME, _TYPE, _FROM_STR, _VALUE)                                      \
        class DECL_##_NAME : public const_decl<_TYPE> {                                         \
        public:                                                                                 \
            using type          = const_decl<_TYPE>::type;                                      \
            using from_str_func = const_decl<_TYPE>::from_str_func;                             \
                                                                                                \
            DECL_##_NAME(const std::string& var_name, from_str_func from_str, type var_default) \
                : const_decl<_TYPE>{var_name, from_str, var_default} { }                        \
                                                                                                \
            DECL_##_NAME(const DECL_##_NAME& other) noexcept                                    \
                : const_decl<_TYPE>{other} { }                                                  \
        };                                                                                      \
        DECL_##_NAME::from_str_func from_str_##_NAME{_FROM_STR};                                \
        DECL_##_NAME                _NAME {                                                     \
            STRINGIFY(_NAME), from_str_##_NAME, _VALUE                           \
        }
#endif // DECLARE_CONST

/**
 * @brief A template base class for constant definitions
 *
 * Placed in the const_registry for global parameter lookups
 *
 * Each specialized derived class holds the constant's name, value, and
 * default value fetched via const accessors
 */
template<typename T>
class const_decl {
public:
    using type          = T;
    using from_str_func = std::function<type(const std::string&)>;

    virtual ~const_decl() = default;

    const_decl(const std::string& var_name, from_str_func from_str, type var_default)
        : _m_name{var_name}
        , _m_from_str{from_str}
        , _m_default{var_default} {
        const std::string value_str{value_str_from_env(_m_name.c_str())};
        _m_value = (value_str.empty()) ? _m_default : _m_from_str(value_str);
#ifndef NDEBUG
        _m_value_str = value_str;
#endif // NDEBUG
    }

    const_decl(const const_decl<type>& other) noexcept
        : _m_name{other._m_name}
        , _m_from_str{other._m_from_str}
        , _m_default{other._m_default}
        , _m_value{other._m_default}
#ifndef NDEBUG
        , _m_value_str{other._m_value_str}
#endif // NDEBUG
    {
    }

    const_decl(const_decl<type>&& other) noexcept; // Immovable

    /*--- Accessors ---*/

    const std::string& name() const noexcept {
        return _m_name;
    }

    const type& value() const noexcept {
        return _m_value;
    }

    const type& value_default() const noexcept {
        return _m_default;
    }

private:
    /**
     * A private static function used to retrieve the value string for the
     * the given variable
     *
     * var_name: A c-style string of the constant's name in the system environment
     *
     * Returns a std::string containing the constant's stringified value
     */
    static std::string value_str_from_env(const char* const var_name) noexcept {
        const char* const c_str{std::getenv(var_name)};
#ifndef NDEBUG
        std::cout << "value_str_from_env(" << var_name << "): " << c_str << std::endl
                  << var_name << " is null? " << ((c_str == nullptr) ? "true" : "false") << std::endl;
#endif // NDEBUG
        return (c_str == nullptr) ? std::string{""} : std::string{c_str};
    }

    /*--- Private members ---*/

    const std::string _m_name;
    from_str_func     _m_from_str;
    const type        _m_default;
    type              _m_value;

#ifndef NDEBUG
    std::string _m_value_str;
#endif // NDEBUG
};

/**
 * @brief A class for constant declarations and their type conversions
 *
 * See DECLARE_CONST and 'common/const_decls.hpp' for usage details
 */
class const_registry : public phonebook::service {
public:
    ~const_registry() = default;

    /*--- Types and converter functions ---*/

    using RawPath = std::string;

    template<typename T>
    static const T& noop(const T& obj) noexcept {
        return obj;
    }

    template<typename T>
    static T copy(const T& obj) noexcept {
        return obj;
    }

    template<typename T>
    static T to(const std::string& value_str);

    /*--- Wrappers needed for a single argument for the `std::sto..` functions ---*/

    DECLARE_TO_FUNC(int, i);
    DECLARE_TO_FUNC(unsigned int, ul);
    DECLARE_TO_FUNC(long, l);
    DECLARE_TO_FUNC(double, d);

    template<> /// Function is static via template specialization
    bool to(const std::string& value_str) {
        /// Converting from Python-style bools
        return (value_str == "True") ? true
            : (value_str == "False") ? false
                                     : throw std::runtime_error("Invalid conversion from std::string to bool");
    }

    /*--- Constant declarations ---*/

#include "const_decls.hpp"
};

} // namespace ILLIXR
