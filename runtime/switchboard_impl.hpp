#include "common/switchboard.hpp"
#include "common/data_format.hpp"
#include <atomic>
#include <vector>
#include <iostream>
#include <cassert>
#include <mutex>

#include "concurrentqueue.hpp"
template <typename T>
using queue = moodycamel::ConcurrentQueue<T>;

/*
Proof of thread-safety:
- Since all instance members are private, proving each method is datarace-free implies the class is.
    - I prove this by showing every access is guarded by a lock, implemented atomically, or uses concurrent primitives (AKA the concurrentqueue.hpp implementatoin).
- All code in this module acquires _m_registry_lock before _m_callbacks_lock, and does not call any external code which could acquire a lock, therefore this is deadlock-free.
- (Bonus) none of the locks are contended in steady-state.

Caveat:
- See caveat on invoke_callbacks()
- See caveat on put()
*/

namespace ILLIXR {

	class topic {
	public:

		class topic_reader_latest : public reader_latest<void> {
		public:
			virtual const void* get_latest_ro() const override {
				/* Proof of thread-safety:
				   - Reads _m_topic, which is const.
				   - Reads _m_topic->_m_latest using atomics.
				*/
				return _m_topic->_m_latest.load();
			}

			virtual void* get_latest() const override {
				/* Proof of thread-safety is TBD, as the implementation is also. */

				/* TODO: (feature) support this method. The immediate problem is that the type has
				   been erased at this point, so I don't know how to copy a void*. */
				throw std::runtime_error{"this doesn't work yet"};
				/* TODO: (optimization) avoid this copy if we are the only subscriber. The reader
				   can read and modify the event in place. */
			}
			topic_reader_latest(const topic* topic) : _m_topic{topic} {
				/* No thread-safety required in constructor. This is only called by one thread. */
			}
			virtual ~topic_reader_latest() {
				/* No thread-safety required in destructor. This is only called by the owning thread. */
			}

		private:
			const topic *const _m_topic;
		};

		class topic_writer : public writer<void> {
		public:
			virtual void* allocate() override {
				/* TODO: (feature:allocate) support this method. The immediate problem is that the
				   type has been erased at this point, so I don't know how to copy a void*. */
				throw std::runtime_error{"this doesn't work yet"};
				/* Note on memory safety: allocated pointers always get moved into put(). Then they
				   are always freed when put() is called again or when the destructor is called. */
				/* TODO: (optimization:free-list) pop from free list if non-empty. */

			}

			virtual void put(const void* contents) override {
				/*
				  Proof of thread-safety:
				   - Reads _m_topic, which is const.
				  - Modifies _m_topic->_m_latest using atomics
				  - Modifies _m_topic->_m_queue using concurrent primitives

				  One caveat:
				  While there is no data-race here, there is a synchronization race.
				  In the case where there are multiple writers to a topic,
				  A reader observing _m_latest could see events in a different order than those observing _m_queue!
				  However, I contend this is not a problem, because I only guarantee that _m_topic->_m_latest has 'sufficiently fresh data',
				  so if 2 events come in at the same time, I don't care which I publish to _m_latest.
				  Also, there is not currently any case where two threads write to the same topic in ILLIXR.
				  I don't want to acquire a lock here because it would be contended.
				*/
				assert(contents);
				const void* old __attribute__((unused)) =
					_m_topic->_m_latest.exchange(contents);

				// delete old;
				/* TODO: (feature:allocate) Free old.*/
				/* TODO: (optimization:free-list) return to free-list. */
				[[maybe_unused]] int ret = _m_topic->_m_queue.enqueue(std::make_pair(_m_topic->_m_name, contents));
				// Unused if the assert is not on.
				assert(ret);
			}

			topic_writer(topic* topic) : _m_topic{topic} {
				/* No need for thread-safety, constructor is only called from one thread. */
			}
			virtual ~topic_writer() {
				/* No need for thread-safety, destructor is only called from owning thread. */
			}

		private:
			/* TODO: (optimization:free-list) maintain a free-list. allocate should pop and return
			   an object from the list. The destructor should put it back on the list. This needs to
			   be done in the destructor, not in `put`, because even though the reader_latest are
			   not reading the object, it could still be scheduled for synchronous processing. */
			/* TODO: (optimization) pre-allocate in a static array. */
			topic * const _m_topic;
		};

		std::unique_ptr<topic_writer> get_writer() {
			/*
			 * Proof of thread-safety:
			 * See topic_writer proof of thread-safety.
			 */
			return std::make_unique<topic_writer>(this);
		}

		std::unique_ptr<topic_reader_latest> get_reader_latest() const {
			/*
			 * Proof of thread-safety:
			 * See topic_reader_latest proof of thread-safety.
			 */
			return std::make_unique<topic_reader_latest>(this);
		}

		void schedule(std::function<void(const void*)> callback) {
			const std::lock_guard<std::mutex> lock{_m_callbacks_lock};
			_m_callbacks.push_back(callback);
		}

		std::size_t ty() {
			/* Proof of thread-safety: ty is immutable*/
			return _m_ty;
		}

		topic(std::size_t ty, const std::string name, queue<std::pair<std::string, const void*>>& queue)
			: _m_ty{ty}
			, _m_name{name}
			, _m_queue{queue}
		{
			/* No need for thread-safety, constructor is only called from one thread. */
		}

		~topic() {
			/*
			 * No need for thread-safety:
			 * Destrutctor should only be called from one thread (the thread owning switchboard)
			 */
			const void* latest = _m_latest.exchange(nullptr);
			if (latest) {
				/* TODO: (feature:allocate) Free old.*/
			}
			/* TODO: (optimization:free-list) free the elements of free-list. */
		}

		void invoke_callbacks(const void* event) {
			/*
			 * Proof of thread-safety:
			 * - All reads _m_callbacks occur after acquiring its lock.
			 * - Callback cannot call `check_queues`, because it is private to this class. Therefore, it cannot cause a deadlock
			 *
			 * Note: this function can only be called by the switchboard_impl::_m_threads, because the topic class is not exposed.
			 * TODO: Should be a private inner-class to guarantee this.
			 *
			 * One caveat:
			 * - callback should not attempt to create a new subscription, publish, or schedule (that would try to acquire _m_registry_lock)
			 */
			const std::lock_guard<std::mutex> lock{_m_callbacks_lock};
			for (std::function<void(const void*)> callback : _m_callbacks) {
				callback(event);
			}
		}

	private:

		const std::size_t _m_ty;
		std::atomic<const void*> _m_latest {nullptr};
		std::vector<std::function<void(const void*)>> _m_callbacks;
		std::mutex _m_callbacks_lock;
		const std::string _m_name;
		queue<std::pair<std::string, const void*>>& _m_queue;
		/* - const because nobody should write to the _m_latest in
		   place. This is not thread-safe.
		   - atomic because it will be accessed from different threads. */
		/* TODO: (optimization) use unique_ptr when there is only one
		   subscriber. */
		/* TODO: (optimization) use relaxed memory_order? */

	};

	const size_t MAX_EVENTS = 127;
	const size_t MAX_THREADS = 1;

	class switchboard_impl : public switchboard {

	public:

		switchboard_impl()
		{
			for (size_t i = 0; i < MAX_THREADS; ++i) {
				_m_threads.push_back(std::thread{[i, this]() {
					;
					std::cout << "thread," << std::this_thread::get_id() << ",switchboard worker," << i << std::endl;
					this->check_queues();
				}});
			}
		}

		virtual void stop() override {
			if (!_m_terminate.load()) {
				_m_terminate.store(true);
				for (std::thread& thread : _m_threads) {
					thread.join();
				}
			}
		}

		virtual ~switchboard_impl() override {
			stop();
		}

	private:

		void check_queues() {
			/*
			  Proof of thread-safety:
			  - Reads _m_terminate using atomics, and I don't care if the value changes after this.
			  - Modifies the queue using concurrent primitives, and I don't care if the queue changes after this.
			  - Reads _m_registry after acquiring its lock, so it can't change while I'm reading.
			      - In the steady state, this lock is uncontended, because its other acquirers are only called during initialization.
			  - Calls invoke_callback, which acquires _m_callbacks_lock, (see its proof of thread-safety).
			  Therefore this method is thread-safe.
			 */
			std::chrono::nanoseconds thread_start = thread_cpu_time();
			while (!_m_terminate.load()) {
				std::pair<std::string, const void*> t;
				if (_m_queue.try_dequeue(t)) {
					const std::lock_guard lock{_m_registry_lock};
					//std::cout << "cpu_timer,switchboard_check_queues," << (thread_cpu_time() - thread_start).count() << std::endl;
					_m_registry.at(t.first).invoke_callbacks(t.second);
					thread_start = thread_cpu_time();
				}
			}
		}

		virtual void _p_schedule(const std::string& topic_name, std::function<void(const void*)> callback, std::size_t ty) override {
			/*
			  Proof of thread-safety:
			  - Reads _m_registry after acquiring its lock (it can't change)
			      - This method is only called during initialization.
			  - Calls topic.schedule, which acquires _m_callbacks_lock, (see its proof of thread-safety)
			  Therefore this method is thread-safe.
			 */
			const std::lock_guard lock{_m_registry_lock};
			topic& topic = _m_registry.try_emplace(topic_name, ty, topic_name, _m_queue).first->second;
			assert(topic.ty() == ty);
			topic.schedule(callback);
		}

		virtual std::unique_ptr<writer<void>> _p_publish(const std::string& topic_name, std::size_t ty) override {
			/*
			  Proof of thread-safety:
			  - All accesses _m_registry occur after acquiring its lock
			      - Never acquires _m_callbacks_lock
			      - This method is only called during initialization, so no steady-state contention.
			  - Returns a writer handle (see its proof of thread-safety)
			  Therefore this method is thread-safe.
			 */
			const std::lock_guard lock{_m_registry_lock};
			topic& topic = _m_registry.try_emplace(topic_name, ty, topic_name, _m_queue).first->second;
			assert(topic.ty() == ty);
			return std::unique_ptr<writer<void>>(topic.get_writer().release());
			/* TODO: (code beautify) why can't I write
			   return std::move(topic.get_writer());
			*/
		}

		virtual std::unique_ptr<reader_latest<void>> _p_subscribe_latest(const std::string& topic_name, std::size_t ty) override {
			/*
			  Proof of thread-safety:
			  - All accesses _m_registry occur after acquiring its lock
			      - This method is only called during initialization, so no steady-state contention.
			      - Does not acquire _m_callbacks_lock
			  - Returns a writer handle (see its proof of thread-safety)
			  Therefore this method is thread-safe.
			 */
			const std::lock_guard lock{_m_registry_lock};
			topic& topic = _m_registry.try_emplace(topic_name, ty, topic_name, _m_queue).first->second;
			assert(topic.ty() == ty);
			return std::unique_ptr<reader_latest<void>>(topic.get_reader_latest().release());
			/* TODO: (code beautify) why can't I write
			   return std::move(topic.get_reader_latest());
			*/
		}

		std::unordered_map<std::string, topic> _m_registry;
		std::mutex _m_registry_lock;
		std::vector<std::thread> _m_threads;
		std::atomic<bool> _m_terminate {false};
		queue<std::pair<std::string, const void*>> _m_queue;

	};

	std::shared_ptr<switchboard> create_switchboard() {
		return std::dynamic_pointer_cast<switchboard>(std::make_shared<switchboard_impl>());
	}
}
