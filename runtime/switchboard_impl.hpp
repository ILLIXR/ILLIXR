#include "common/switchboard.hpp"
#include "common/data_format.hpp"
#include <atomic>
#include <vector>
#include <iostream>
#include <cassert>

#include "concurrentqueue.hpp"
template <typename T>
using queue = moodycamel::ConcurrentQueue<T>;

namespace ILLIXR {

	class topic {
	public:

		class topic_reader_latest : public reader_latest<void> {
		public:
			virtual const void* get_latest_ro() const override {
				return _m_topic->_m_latest.load();
			}

			virtual void* get_latest() const override {
				/* TODO: (feature) support this method. The immediate
				   problem is that the type has been erased at this
				   point, so I don't know how to copy a void*. */
				throw std::runtime_error{"this doesn't work yet"};
				/* TODO: (optimization) avoid this copy if we are the
				   only subscriber. The reader can read and modify the
				   event in place. */
			}
			topic_reader_latest(const topic* topic) : _m_topic{topic} { }
			virtual ~topic_reader_latest() { }

		private:
			const topic *const _m_topic;
		};

		class topic_writer : public writer<void> {
		public:
			virtual void* allocate() override {
				/* TODO: (feature:allocate) support this method. The
				   immediate problem is that the type has been erased
				   at this point, so I don't know how to copy a
				   void*. */
				throw std::runtime_error{"this doesn't work yet"};
				/* Note on memory safety: allocated pointers always
				   get moved into put(). Then they are always freed
				   when put() is called again or when the destructor
				   is called. */
				/* TODO: (optimization:free-list) pop from free list if non-empty. */

			}

			virtual void put(const void* contents) override {
				assert(contents);
				const void* old __attribute__((unused)) =
					_m_topic->_m_latest.exchange(contents);

				// delete old;
				/* TODO: (feature:allocate) Free old.*/
				/* TODO: (optimization:free-list) return to free-list. */

				assert((
					_m_topic->_m_queue.enqueue(
						std::make_pair(_m_topic->_m_name, contents)
					)
				));
			}

			topic_writer(topic* topic) : _m_topic{topic} { }
			virtual ~topic_writer() { }

		private:
			/* TODO: (optimization:free-list) maintain a free-list. allocate
			   should pop and return an object from the list. The
			   destructor should put it back on the list. This needs
			   to be done in the destructor, not in `put`, because
			   even though the reader_latest are not reading the
			   object, it could still be scheduled for synchronous
			   processing. */
			/* TODO: (optimization) pre-allocate in a static array. */
			topic * const _m_topic;
		};

		std::unique_ptr<topic_writer> get_writer() {
			return std::make_unique<topic_writer>(this);
		}

		std::unique_ptr<topic_reader_latest> get_reader_latest() const {
			return std::make_unique<topic_reader_latest>(this);
		}

		void schedule(std::function<void(const void*)> callback) {
			_m_callbacks.push_back(callback);
		}

		std::size_t ty() { return _m_ty; }

		topic(std::size_t ty, const std::string name, queue<std::pair<std::string, const void*>>& queue)
			: _m_ty{ty}
			, _m_name{name}
			, _m_queue{queue}
		{ }

		~topic() {
			const void* latest = _m_latest.exchange(nullptr);
			if (latest) {
				/* TODO: (feature:allocate) Free old.*/
			}
			/* TODO: (optimization:free-list) free the elements of free-list. */
		}

		const std::vector<std::function<void(const void*)>>& callbacks() {
			return _m_callbacks;
		}

	private:

		const std::size_t _m_ty;
		std::atomic<const void*> _m_latest {nullptr};
		std::vector<std::function<void(const void*)>> _m_callbacks;
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
				_m_threads.push_back(std::thread{[this]() {
					this->check_queues();
				}});
			}
		}

		virtual void stop() {
			_m_terminate.store(true);
			for (std::thread& thread : _m_threads) {
				thread.join();
			}
		}

		virtual ~switchboard_impl() {
		}

	private:

		void check_queues() {
			while (!_m_terminate.load()) {
				std::pair<std::string, const void*> t;
				if (_m_queue.try_dequeue(t)) {
					for (std::function<void(const void*)> callback : _m_registry.at(t.first).callbacks()) {
						// std::cout << "Callback: " << t.first << ", " << t.second << std::endl;
						callback(t.second);
					}
				}
			}
		}

		virtual void _p_schedule(const std::string& name, std::function<void(const void*)> callback, std::size_t ty) {
			_m_registry.try_emplace(name, ty, name, _m_queue);
			_m_registry.at(name).schedule(callback);
		}

		virtual std::unique_ptr<writer<void>> _p_publish(const std::string& name, std::size_t ty) {
			_m_registry.try_emplace(name, ty, name, _m_queue);
			topic& topic = _m_registry.at(name);
			assert(topic.ty() == ty);
			return std::unique_ptr<writer<void>>(topic.get_writer().release());
			/* TODO: (code beautify) why can't I write
			   return std::move(topic.get_writer());
			*/
		}

		virtual std::unique_ptr<reader_latest<void>> _p_subscribe_latest(const std::string& name, std::size_t ty) {
			_m_registry.try_emplace(name, ty, name, _m_queue);
			topic& topic = _m_registry.at(name);
			assert(topic.ty() == ty);
			return std::unique_ptr<reader_latest<void>>(topic.get_reader_latest().release());
			/* TODO: (code beautify) why can't I write
			   return std::move(topic.get_reader_latest());
			*/
		}

		std::unordered_map<std::string, topic> _m_registry;
		std::vector<std::thread> _m_threads;
		std::atomic<bool> _m_terminate {false};
		queue<std::pair<std::string, const void*>> _m_queue;

	};

	std::unique_ptr<switchboard> create_switchboard() {
		return std::unique_ptr<switchboard>{new switchboard_impl};
	}
}
