#pragma once

#include <cassert>
#include <iostream>
#include <memory>
#include <shared_mutex>
#include <stdexcept>
#include <typeindex>
#include <unordered_map>

namespace ILLIXR {

/**
 * @brief A [service locator][1] for ILLIXR.
 *
 * This will be explained through an exmaple: Suppose one dynamically-loaded plugin, `A_plugin`,
 * needs a service, `B_service`, provided by another, `B_plugin`. `A_plugin` cannot statically
 * construct a `B_service`, because the implementation `B_plugin` is dynamically
 * loaded. However, `B_plugin` can register an implementation of `B_service` when it is loaded,
 * and `A_plugin` can lookup that implementation without knowing it.
 *
 * `B_service.hpp` in `common`:
 * \code{.cpp}
 * class B_service {
 * public:
 *     virtual void frobnicate(foo data) = 0;
 * };
 * \endcode
 *
 * `B_plugin.hpp`:
 * \code{.cpp}
 * class B_impl : public B_service {
 * public:
 *     virtual void frobnicate(foo data) {
 *         // ...
 *     }
 * };
 * void blah_blah(phonebook* pb) {
 *     // Expose `this` as the "official" implementation of `B_service` for this run.
 *     pb->register_impl<B_service>(std::make_shared<B_impl>());
 * }
 * \endcode
 *
 * `A_plugin.cpp`:
 * \code{.cpp}
 * #include "B_service.hpp"
 * void blah_blah(phonebook* pb) {
 *     B_service* b = pb->lookup_impl<B_service>();
 *     b->frobnicate(data);
 * }
 * \endcode
 *
 * If the implementation of `B_service` is not known to `A_plugin` (the usual case), `B_service
 * should be an [abstract class][2]. In either case `B_service` should be in `common`, so both
 * plugins can refer to it.
 *
 * One could even selectively return a different implementation of `B_service` depending on the
 * caller (through the parameters), but we have not encountered the need for that yet.
 *
 * [1]: https://en.wikipedia.org/wiki/Service_locator_pattern
 * [2]: https://en.wikibooks.org/wiki/C%2B%2B_Programming/Classes/Abstract_Classes
 */
class phonebook {
    /*
      Proof of thread-safety:
      - Since all instance members are private, acquiring a lock in each method implies the class is datarace-free.
      - Since there is only one lock and this does not call any code containing locks, this is deadlock-free.
      - Both of these methods are only used during initialization, so the locks are not contended in steady-state.

      However, to write a correct program, one must also check the thread-safety of the elements
      inserted into this class by the caller.
    */

public:
    /**
     * @brief A 'service' that can be registered in the phonebook.
     *
     * These must be 'destructible', have a virtual destructor that phonebook can call in its
     * destructor.
     */
    class service {
    public:
        /**
         */
        virtual ~service() { }
    };

    /**
     * @brief Registers an implementation of @p baseclass for future calls to lookup.
     *
     * Safe to be called from any thread.
     *
     * The implementation will be owned by phonebook (phonebook calls `delete`).
     */
    template<typename specific_service>
    void register_impl(std::shared_ptr<specific_service> impl) {
        const std::unique_lock<std::shared_mutex> lock{_m_mutex};

        const std::type_index type_index = std::type_index(typeid(specific_service));
#ifndef NDEBUG
        std::cerr << "Register " << type_index.name() << std::endl;
#endif
        assert(_m_registry.count(type_index) == 0);
        _m_registry.try_emplace(type_index, impl);
    }

    /**
     * @brief Look up an implementation of @p specific_service, which should be registered first.
     *
     * Safe to be called from any thread.
     *
     * Do not call `delete` on the returned object; it is still managed by phonebook.
     *
     * @throws if an implementation is not already registered.
     */
    template<typename specific_service>
    std::shared_ptr<specific_service> lookup_impl() const {
        const std::shared_lock<std::shared_mutex> lock{_m_mutex};

        const std::type_index type_index = std::type_index(typeid(specific_service));

#ifndef NDEBUG
        // if this assert fails, and there are no duplicate base classes, ensure the hash_code's are unique.
        if (_m_registry.count(type_index) != 1) {
            throw std::runtime_error{"Attempted to lookup an unregistered implementation " + std::string{type_index.name()}};
        }
#endif

        std::shared_ptr<service> this_service = _m_registry.at(type_index);
        assert(this_service);

        std::shared_ptr<specific_service> this_specific_service = std::dynamic_pointer_cast<specific_service>(this_service);
        assert(this_specific_service);

        return this_specific_service;
    }

private:
    std::unordered_map<std::type_index, const std::shared_ptr<service>> _m_registry;
    mutable std::shared_mutex                                           _m_mutex;
};
} // namespace ILLIXR
