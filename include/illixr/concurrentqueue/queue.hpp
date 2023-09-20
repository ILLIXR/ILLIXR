#include <chrono>
#include <mutex>
#include <list>
#ifndef NDEBUG
#include <spdlog/spdlog.h>
#endif
#include <thread>

namespace moodycamel {
	template <typename T>
	class LockQueue {
	private:
		std::mutex mut;
		std::list<T> list;
	public:
		LockQueue(size_t) { }

		bool try_dequeue(T& elem) {
#ifndef NDEBUG
            spdlog::get("illixr")->debug("[queue] try_dequeue");
#endif
			std::lock_guard{mut};
			if (!list.empty()) {
				elem = list.front();
				list.pop_front();
				return true;
			}
			return false;
		}

		bool wait_dequeue_timed(T& elem, size_t usecs_) {
            spdlog::get("illixr")->debug("[queue] wait_dequeue_timed");
			std::chrono::microseconds usecs {usecs_};
			auto start = std::chrono::system_clock::now();
			while (std::chrono::system_clock::now() < start + usecs) {
				if (try_dequeue(elem)) {
					return true;
				}
				std::this_thread::sleep_for(usecs);
			}
			return false;
		}

		bool enqueue(T&& elem) {
            spdlog::get("illixr")->debug("[queue] enqueue");
			std::lock_guard{mut};
			list.emplace_back(elem);
			return true;
		}
	};
}
