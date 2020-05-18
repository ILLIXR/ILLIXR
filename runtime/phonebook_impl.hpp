#ifndef PHONEBOOK_IMPL_HH
#define PHONEBOOK_IMPL_HH

#include <unordered_map>
#include <memory>
#include <stdexcept>
#include <cassert>
#include <mutex>
#include "common/phonebook.hpp"

namespace ILLIXR {

	class phonebook_impl : public phonebook {
		/*
		  Proof of thread-safety:
		  - Since all instance members are private, acquiring a lock in each method implies the class is datarace-free.
		  - Since there is only one lock and this does not call any code containing locks, this is deadlock-free.
		  - Both of these methods are only used during initialization, so the locks are not contended in steady-state.

		  However, to write a correct program, one must also check the thread-safety of the elements
		  inserted into this class by the caller.
		*/

	private:
		virtual service* _p_lookup_impl(const std::type_index& info) {
			const std::lock_guard<std::mutex> lock{_m_mutex};
			// if this assert fails, and there are no duplicate base classes, ensure the hash_code's are unique.
			if (_m_registry.count(info) != 1) {
				throw std::runtime_error{"Attempted to lookup an unregistered implementation " + std::string{info.name()}};
			}
			return _m_registry.at(info).get();
		}
		virtual void _p_register_impl(const std::type_index& info, service* impl) {
			const std::lock_guard<std::mutex> lock{_m_mutex};
			_m_registry.erase(info);
			_m_registry.try_emplace(info, impl);
		}
		std::unordered_map<std::type_index, std::unique_ptr<service>> _m_registry;
		std::mutex _m_mutex;
	};

	std::unique_ptr<phonebook> create_phonebook() {
		return std::unique_ptr<phonebook>{new phonebook_impl};
	}

}

#endif
