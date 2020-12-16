#pragma once

#include <iostream>
#include <cstdlib>
#include <string>
#include <stdexcept>
#include <string_view>
#include <functional>

#include "phonebook.hpp"


namespace ILLIXR
{

/**
 * @brief Convert a string containing a (python) boolean to the bool type
 */
inline bool str_to_bool(std::string var) {
    return (var == "True")  ? true  :
           (var == "False") ? false :
           throw std::runtime_error("Invalid conversion from std::string to bool");
}


/*--- Declarations ---*/
/**
 * (Macro expansion) Function used to create string literals from variable names
 *
 * _X: The literal text (i.e. variable name) to be stringified
 */
#ifndef STRINGIFY
#define STRINGIFY_HELP(_X) #_X
#define STRINGIFY(_X) STRINGIFY_HELP(_X)
#endif // STRINGIFY


/**
 * (Macro expansion) Function used to convert to a type from a string
 *
 * _TYPE:   The type name returned by the function
 * _SUFFIX: The suffix to be applied for the std::sto.. function called
 */
#ifndef DECLARE_TO_FUNC
#define DECLARE_TO_FUNC(_TYPE, _SUFFIX)             \
template<>                                          \
_TYPE to(const std::string& value_str) noexcept     \
{                                                   \
    return std::sto##_SUFFIX(value_str);            \
}
#endif // DECLARE_TO_FUNC


/**
 * (Macro expansion) Function used to declare constants used by modules
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
#define DECLARE_CONST(_NAME, _TYPE, _FROM_STR, _VALUE)                  \
class DECL_##_NAME : public const_decl<_TYPE>                           \
{                                                                       \
public:                                                                 \
    using type          = const_decl<_TYPE>::type;                      \
    using from_str_func = const_decl<_TYPE>::from_str_func;             \
                                                                        \
    DECL_##_NAME(                                                       \
        const std::string& var_name,                                    \
        from_str_func from_str,                                         \
        type var_default                                                \
    ) noexcept                                                          \
        : const_decl<_TYPE>{var_name, from_str, var_default}            \
    { }                                                                 \
                                                                        \
    DECL_##_NAME(const DECL_##_NAME& other) noexcept                    \
        : const_decl<_TYPE>{other}                                      \
    { }                                                                 \
};                                                                      \
DECL_##_NAME::from_str_func from_str_##_NAME { _FROM_STR };             \
DECL_##_NAME _NAME { STRINGIFY(_NAME), from_str_##_NAME, _VALUE }
#endif // DECLARE_CONST


/**
 * A template base class for constant definitions to be provided to the
 * const_registry for global parameter lookups
 *
 * Each specialized derived class holds the constant's name, value, and
 * default value fetched via const accessors
 */
template<typename T>
class const_decl
{
public:
    using type = T;
    using from_str_func = std::function< type (const std::string&) >;

    virtual ~const_decl() = default;

    const_decl(
        const std::string& var_name,
        from_str_func from_str,
        type var_default
    ) noexcept
        : _m_name{var_name}
        , _m_from_str{from_str}
        , _m_default{var_default}
    {
        const std::string value_str { value_str_from_env(_m_name.c_str()) };
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
    { }

    const_decl(const_decl<type>&& other) noexcept; // Immovable

    /*--- Accessors ---*/

    const std::string& name() const noexcept
    {
        return _m_name;
    }

    const type& value() const noexcept
    {
        return _m_value;
    }

    const type& value_default() const noexcept
    {
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
    static std::string value_str_from_env(const char* const var_name) noexcept
    {
        const char* const c_str { std::getenv(var_name) };
#ifndef NDEBUG
        std::cout << "value_str_from_env(" << var_name << "): " << c_str
                  << std::endl
                  << var_name << " is null? " << ((c_str == nullptr) ? "true" : "false")
                  << std::endl;
#endif // NDEBUG
        return (c_str == nullptr) ? std::string{""} : std::string{c_str};
    }

    /*--- Private members ---*/

    const std::string _m_name;
    from_str_func _m_from_str;
    const type _m_default;
    type _m_value;

#ifndef NDEBUG
    std::string _m_value_str;
#endif // NDEBUG
};


class const_registry : public phonebook::service
{
public:
    ~const_registry() = default;

    /*--- Types and converter functions ---*/

    using RawPath = std::string;

    template<typename T>
    static const T& noop(const T& obj) noexcept
    {
        return obj;
    }

    template<typename T>
    static T copy(const T& obj) noexcept
    {
        return obj;
    }

    template<typename T>
    static T to(const std::string& value_str) noexcept;

    /*--- Wrappers needed for a single argument for the `std::sto..` functions ---*/

    DECLARE_TO_FUNC(int,    i);
    DECLARE_TO_FUNC(long,   l);
    DECLARE_TO_FUNC(double, d);

    /*--- Constant declarations ---*/

#define DATA_PATH DATA_PATH
    DECLARE_CONST(DATA_PATH,     RawPath, noop<RawPath>, "/dev/null");

#define DEMO_OBJ_PATH DEMO_OBJ_PATH    
    DECLARE_CONST(DEMO_OBJ_PATH, RawPath, noop<RawPath>, "/dev/null");

#define FB_WIDTH FB_WIDTH
    DECLARE_CONST(FB_WIDTH,      int,     to<int>,       2560); // Pixels

#define FB_HEIGHT FB_HEIGHT
    DECLARE_CONST(FB_HEIGHT,     int,     to<int>,       1440); // Pixels

#define RUN_DURATION RUN_DURATION
    DECLARE_CONST(RUN_DURATION,  long,    to<int>,       60L ); // Seconds

#define REFRESH_RATE REFRESH_RATE
    DECLARE_CONST(REFRESH_RATE,  double,  to<double>,    60.0); // Hz
};

}
