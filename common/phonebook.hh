#ifndef PHONEBOOK_HH
#define PHONEBOOK_HH

namespace ILLIXR {

	class deconstructible {
	public:
		virtual ~deconstructible() = 0;
	};

	class phonebook {
	public:
		template <typename baseclass>
		void register_impl(baseclass* impl) {
			_p_register_impl(typeid(baseclass).hash_code(), static_cast<deconstructible*>(impl));
		}

		template <typename baseclass>
		baseclass* lookup_impl() {
			// if this cast fails, ensure the hash_code's are unique.
			return dynamic_cast<baseclass*>(_p_lookup_impl(typeid(baseclass).hash_code()));
		}

	private:
		virtual deconstructible* _p_lookup_impl(std::size_t info) = 0;
		virtual void _p_register_impl(std::size_t info, deconstructible* impl) = 0;
	};
}

#endif
