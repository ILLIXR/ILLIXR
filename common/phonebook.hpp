#ifndef PHONEBOOK_HH
#define PHONEBOOK_HH

#include <typeindex>

namespace ILLIXR {

	class service {
	public:
		virtual ~service() {}
	};

	class phonebook {
	public:
		template <typename baseclass>
		void register_impl(baseclass* impl) {
			_p_register_impl(std::type_index(typeid(baseclass)), static_cast<service*>(impl));
		}

		template <typename baseclass>
		baseclass* lookup_impl() {
			// if this cast fails, ensure the hash_code's are unique.
			return dynamic_cast<baseclass*>(_p_lookup_impl(std::type_index(typeid(baseclass))));
		}

		virtual ~phonebook() { }

	private:
		virtual service* _p_lookup_impl(const std::type_index& info) = 0;
		virtual void _p_register_impl(const std::type_index& info, service* impl) = 0;
	};
}

#endif
