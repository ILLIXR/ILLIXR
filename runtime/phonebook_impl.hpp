#ifndef PHONEBOOK_IMPL_HH
#define PHONEBOOK_IMPL_HH

#include <unordered_map>
#include <memory>
#include <stdexcept>
#include <cassert>
#include "common/phonebook.hpp"

namespace ILLIXR {

	class phonebook_impl : public phonebook {
	private:
		virtual service* _p_lookup_impl(const std::type_index& info) {
			// if this assert fails, and there are no duplicate base classes, ensure the hash_code's are unique.
			if (_m_registry.count(info) != 1) {
				throw std::runtime_error{"Attempted to lookup an unregistered implementation " + std::string{info.name()}};
			}
			return _m_registry.at(info).get();
		}
		virtual void _p_register_impl(const std::type_index& info, service* impl) {
			// if this assert fails, and there are no duplicate base classes, ensure the hash_code's are unique.
			if (_m_registry.count(info) != 0) {
				throw std::runtime_error{"Attempted to register multiple implementations for " + std::string{info.name()}};
			}
			_m_registry.try_emplace(info, impl);
		}
		std::unordered_map<std::type_index, std::unique_ptr<service>> _m_registry;
	};

	std::unique_ptr<phonebook> create_phonebook() {
		return std::unique_ptr<phonebook>{new phonebook_impl};
	}

}

#endif
