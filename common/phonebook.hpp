#ifndef PHONEBOOK_HH
#define PHONEBOOK_HH

#include <typeindex>
#include <stdexcept>
#include <cassert>

namespace ILLIXR {
	/**
	 * @brief A 'service' that can be registered in the phonebook.
	 * 
	 * These must be 'destructible', have a virtual destructor that phonebook can call in its
	 * destructor.
	 */
	class service {
	public:
		virtual ~service() {}
	};

	/**
	 * @brief A [service locator][1] for ILLIXR.
	 *
	 * Suppose one dynamically-loaded plugin, `A_plugin`, needs a service, `B_service`, provided by
	 * another, `B_plugin`. `A_plugin` cannot statically construct a `B_service`, because the
	 * implementation `B_plugin` is dynamically loaded. However, `B_plugin` can register an
	 * implementation of `B_service` when it is loaded, and `A_plugin` can lookup that
	 * implementation without knowing it.
	 *
	 * One could even selectively return a different implementation of `B_service` depending on the
	 * caller (through the parameters), but we have not encountered the need for that yet.
	 * 
	 * [1]: https://en.wikipedia.org/wiki/Service_locator_pattern
	 */
	class phonebook {
	public:
		/**
		 * @brief Registers an implementation of @p baseclass for future calls to lookup.
		 *
		 * This overwwrites any existing implementation of @p baseclass.
		 *
		 * The implementation will be owned by phonebook (phonebook calls `delete`).
		 */
		template <typename baseclass>
		void register_impl(baseclass* impl) {
			_p_register_impl(std::type_index(typeid(baseclass)), static_cast<service*>(impl));
		}

		/**
		 * @brief Look up an implementation of @p baseclass, which should be registered first.
		 *
		 * Do not call `delete` on the returned object; it is still managed by phonebook.
		 *
		 * @throws if an implementation is not already registered.
		 */
		template <typename baseclass>
		baseclass* lookup_impl() {
			baseclass* ret = dynamic_cast<baseclass*>(_p_lookup_impl(std::type_index(typeid(baseclass))));
			assert(ret);
			return ret;
		}

		virtual ~phonebook() { }

	private:
		virtual service* _p_lookup_impl(const std::type_index& info) = 0;
		virtual void _p_register_impl(const std::type_index& info, service* impl) = 0;
	};
}

#endif
