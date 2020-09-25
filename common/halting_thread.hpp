#include <cassert>
#include <thread>
#include <iostream>
#include <functional>
#include <atomic>

template <typename halting_thread_impl>
class halting_thread {
protected:
	std::atomic<bool> _m_terminate {false};
	std::thread _m_thread;

	void thread_body() {
		std::cerr << "thread_body() was not overridden" << std::endl;
		abort();
	}

	void thread_exit() { }

public:
	halting_thread(halting_thread&& other) noexcept
		: _m_thread{std::move(other._m_thread)}
	{ }

	halting_thread& operator=(halting_thread&& other) noexcept {
		// Assert my thread was a dummy
		assert(!_m_thread.joinable());
		_m_thread = other._m_thread;
		_m_terminate = other._m_terminate.load();
	}

	halting_thread() noexcept { }

	~halting_thread() {
		stop();
	}

	void start() {
		// _m_thread = std::thread{std::bind(&halting_thread::thread_main, *this)};
		_m_thread = std::thread{[this]{
			while (!this->_m_terminate.load()) {
				static_cast<halting_thread_impl&>(*this).thread_body();
			}
			static_cast<halting_thread_impl&>(*this).thread_exit();
		}};
	}

	bool started() {
		return _m_thread.joinable() || _m_terminate.load();
	}

	bool stopped() {
		return _m_terminate.load();
	}

	void stop() {
		_m_terminate.store(true);
		_m_thread.join();
	}
};
