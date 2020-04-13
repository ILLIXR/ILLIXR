#ifndef PHONEBOOK_IMPL_HH
#define PHONEBOOK_IMPL_HH

#include <unordered_map>
#include <memory>
#include <stdexcept>
#include <cassert>
#include "common/phonebook.hh"

namespace ILLIXR {

	class phonebook_impl : public phonebook {
	private:
		virtual deconstructible* _p_lookup_impl(std::size_t info) {
			// if this assert fails, and there are no duplicate base classes, ensure the hash_code's are unique.
			if (_m_registry.count(info) != 1) {
				throw std::runtime_error{"Attempted to lookup an unregistered implementation"};
			}
			return _m_registry.at(info).get();
		}
		virtual void _p_register_impl(std::size_t info, deconstructible* impl) {
			// if this assert fails, and there are no duplicate base classes, ensure the hash_code's are unique.
			if (_m_registry.count(info) != 0) {
				throw std::runtime_error{"Attempted to register multiple implementations"};
			}
			_m_registry.try_emplace(info, impl);
		}
		std::unordered_map<std::size_t, std::unique_ptr<deconstructible>> _m_registry;
	};

	std::unique_ptr<phonebook> create_phonebook() {
		return std::unique_ptr<phonebook>{new switchboard_impl};
	}

}

#endif
