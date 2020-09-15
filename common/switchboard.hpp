#pragma once

#include <memory>
#include <atomic>
#include <functional>
#include <chrono>
#include "phonebook.hpp"
#include "cpu_timer.hpp"
#include "record_logger.hpp"
#include "../runtime/concurrentqueue/blockingconcurrentqueue.hpp"

namespace ILLIXR {

/**
 * @brief The type of shared pointer returned by switchboard.
 *
 * Should be a subtype of Switchboard.
 *
 * TODO: Make this agnostic to the type of `ptr``
 * Currently, it depends on `ptr` == shared_ptr
 */
template <typename specific_event>
using ptr = std::shared_ptr<specific_event>;

class event {
public:
	virtual ~event() { }
};

template <typename underlying_type>
class event_wrapper : public event {
private:
	underlying_type underlying_data;
public:
	event_wrapper() { }
	event_wrapper(underlying_type underlying_data_)
		: underlying_data{underlying_data_}
	{ }
	operator underlying_type() const { return underlying_data; }
	underlying_type& operator*() { return underlying_data; }
	const underlying_type& operator*() const { return underlying_data; }
};


/**
 * @Should be private to Switchboard.
 */
const record_header __switchboard_callback_header {"switchboard_callback", {
	{"plugin_id", typeid(std::size_t)},
	{"topic_name", typeid(std::string)},
	{"iteration_no", typeid(std::size_t)},
	{"cpu_time_start", typeid(std::chrono::nanoseconds)},
	{"cpu_time_stop" , typeid(std::chrono::nanoseconds)},
	{"wall_time_start", typeid(std::chrono::high_resolution_clock::time_point)},
	{"wall_time_stop" , typeid(std::chrono::high_resolution_clock::time_point)},
}};

/**
 * @Should be private to Switchboard.
 */
const record_header __switchboard_topic_stop_header {"switchboard_topic_stop", {
	{"plugin_id", typeid(std::size_t)},
	{"topic_name", typeid(std::string)},
	{"processed", typeid(std::size_t)},
	{"unprocessed", typeid(std::size_t)},
	{"idle_cycles", typeid(std::size_t)},
}};

class topic_subscription {
private:
	const std::string& _m_topic_name;
	const std::size_t _m_plugin_id;
	std::function<void(ptr<const event>, std::size_t)> _m_callback;
	const std::shared_ptr<record_logger> _m_record_logger;
	record_coalescer _m_cb_log;
	std::thread _m_thread;
	std::atomic<bool> _m_terminate {false};
	moodycamel::BlockingConcurrentQueue<ptr<const event>> _m_queue {8 /*max size estimate*/};
	static constexpr std::chrono::milliseconds _m_queue_timeout {100};

	void thread_main() {
		std::size_t _m_unprocessed = 0;
		std::size_t _m_processed = 0;
		std::size_t _m_idle_cycles = 0;

		while (!_m_terminate.load()) {
			// Try to pull event off of queue
			ptr<const event> this_event;
			std::int64_t timeout_usecs = std::chrono::duration_cast<std::chrono::microseconds>(_m_queue_timeout).count();
			if (_m_queue.wait_dequeue_timed(this_event, timeout_usecs)) {
				// Process event
				// Also, record and log the time
				_m_processed++;
				auto cb_start_cpu_time  = thread_cpu_time();
				auto cb_start_wall_time = std::chrono::high_resolution_clock::now();
				_m_callback(this_event, _m_processed);
				_m_cb_log.log(record{__switchboard_callback_header, {
					{_m_plugin_id},
					{_m_topic_name},
					{_m_processed},
					{cb_start_cpu_time},
					{thread_cpu_time()},
					{cb_start_wall_time},
					{std::chrono::high_resolution_clock::now()},
				}});
			} else {
				// Nothing to do.
				_m_idle_cycles++;
			}
		}

		// Drain queue
		ptr<const event> this_event;
		while (!_m_queue.try_dequeue(this_event)) {
			_m_unprocessed++;
		}

		// Log stats
		_m_record_logger->log(record{__switchboard_topic_stop_header, {
			{_m_topic_name},
			{_m_processed},
			{_m_unprocessed},
			{_m_idle_cycles},
		}});
	}

public:
	topic_subscription(const std::string& topic_name, std::size_t plugin_id, std::function<void(ptr<const event>, std::size_t)> callback, std::shared_ptr<record_logger> record_logger_)
		: _m_topic_name{topic_name}
		, _m_plugin_id{plugin_id}
		, _m_callback{callback}
		, _m_record_logger{record_logger_}
		, _m_cb_log{record_logger_}
		, _m_thread{std::bind(&topic_subscription::thread_main, this)}
	{ }

	int enqueue(ptr<const event> this_event) {
		return _m_queue.try_enqueue(this_event);
	}

	~topic_subscription() {
		if (!_m_terminate.load()) {
			_m_terminate.store(true);
			_m_thread.join();
		}
	}
};

/**
 * @brief Represents a topic
 *
 * Should be private to Switchboard.
 */
class topic {
private:
	const std::string _m_name;
	const std::type_info& _m_ty;
	const std::shared_ptr<record_logger> _m_record_logger;
	std::atomic<ptr<const event> *> _m_latest {nullptr};
	std::vector<topic_subscription> _m_subscriptions;
	std::mutex _m_subscriptions_lock;

public:
	topic(std::string name, const std::type_info& ty, std::shared_ptr<record_logger> record_logger_)
		: _m_name{name}
		, _m_ty{ty}
		, _m_record_logger{record_logger_}
	{ }

	topic(const topic&) = delete;

	topic& operator=(const topic&) = delete;

	const std::type_info& ty() { return _m_ty; }

	ptr<const event> get_ro() const {
		ptr<const event> ret = *_m_latest;
		assert(ret);
		return ret;
	}

	void put(ptr<const event>* this_event) {
		assert(this_event);
		/* The pointer that this gets exchanged with gets dropped immediately. */
		ptr<const event>* old_event = _m_latest.exchange(this_event);
		delete old_event;

		std::lock_guard<std::mutex> lock{_m_subscriptions_lock};
		for (topic_subscription& ts : _m_subscriptions) {
			[[maybe_unused]] int ret = ts.enqueue(std::const_pointer_cast<const event>(*this_event));
			// If the NDEBUG is defined, this assert is noop, and ret is unused.
			assert(ret);
		}
	}

	void schedule(std::size_t plugin_id, std::function<void(ptr<const event>, std::size_t)> callback) {
		const std::lock_guard<std::mutex> lock{_m_subscriptions_lock};
		_m_subscriptions.emplace_back(_m_name, plugin_id, callback, _m_record_logger);
	}

	~topic() {
		ptr<const event>* last_event = _m_latest.exchange(nullptr);
		delete last_event;
		// corresponds to new in most recent topic::writer::put or topic::topic (if put was never called)

		const std::lock_guard<std::mutex> lock{_m_subscriptions_lock};
		_m_subscriptions.clear();
	}
};

/**
 * @brief A manager for typesafe, threadsafe, named event-streams (called
 * topics).
 *
 * - Writing: One can write to a topic (in any thread) through the
 * `ILLIXR::writer` returned by `publish()`.
 *
 * - There are two ways of reading: asynchronous reading and synchronous
 * reading:
 * [1]: https://en.wikipedia.org/wiki/Slab_allocation
 * [2]: https://en.wikipedia.org/wiki/Multiple_buffering
 *
 *   - Asynchronous reading returns the most-recent event on the topic
 * (idempotently). One can do this through (in any thread) the
 * `ILLIXR::reader_latest` handle returned by `subscribe_latest()`.
 *
 *   - Synchronous reading schedules a callback to be executed on _every_ event
 * which gets published. One can schedule computation by `schedule()`, which
 * will run the computation in a thread managed by switchboard.
 *
 * \code{.cpp}
 * void do_stuff(switchboard* sb) {
 *     auto topic1 = sb->subscribe_latest<topic1_type>("topic1");
 *     auto topic2 = sb->publish<topic2_type>("topic2");
 * 
 *     // Read topic 3 synchronously
 *     sb->schedule<topic3_type>("topic3", [&](const topic3_type *event3) {
 *         // This is a lambda expression
 *         // https://en.cppreference.com/w/cpp/language/lambda
 *         std::cout << "Got a new event on topic3: " << event3 << std::endl;
 *     });
 *
 *     while (true) {
 *         // Read topic 1
 *         topic1_type* event1 = topic1.get_latest_ro();
 *
 *         // Write to topic 2
 *         topic2_type* event2 = new topic2_type;
 *         topic2.put(event2);
 *     }
 * }
 * \endcode
 */
class switchboard : public phonebook::service {
private:
	std::unordered_map<std::string, topic> _m_registry;
	std::mutex _m_registry_lock;
	std::shared_ptr<record_logger> _m_record_logger;

	template <typename specific_event>
	topic& get_or_create_topic(const std::string& topic_name) {
		const std::lock_guard lock{_m_registry_lock};
		topic& topic_ = _m_registry.try_emplace(topic_name, topic_name, typeid(specific_event), _m_record_logger).first->second;
		assert(typeid(specific_event) == topic_.ty());
		return topic_;
	}

public:

	switchboard(const phonebook* pb)
		: _m_record_logger{pb->lookup_impl<record_logger>()}
	{ }

	/**
	 * @brief A handle which can read the latest event on a topic.
	 */
	template <typename specific_event>
	class reader {
	private:
		topic& _m_topic;
	public:
		reader(topic& topic_)
			: _m_topic{topic_}
		{ }

		/**
		 * @brief Gets a "read-only" copy of the latest value.
		 */
		virtual ptr<const specific_event> get_ro() const {
			assert(typeid(specific_event) == _m_topic.ty());
			ptr<const event> this_event = _m_topic.get_ro();
			ptr<const specific_event> this_specific_event = std::dynamic_pointer_cast<const specific_event>();
			assert(!this_event || this_specific_event);
			return this_event;
		}

		/**
		 * @brief Gets a mutable copy of the latest value.
		 */
		virtual ptr<specific_event> get() const {
			assert(typeid(specific_event) == _m_topic.ty());
			ptr<const event> this_event = _m_topic.get_ro();
			ptr<const specific_event> this_specific_event = std::dynamic_pointer_cast<const specific_event>();
			assert(!this_event || this_specific_event);
			return std::make_shared<specific_event>(*this_event);
		}
	};

	/**
	 * @brief A handle which can publish events to a topic.
	 */
	template <typename specific_event>
	class writer {
	private:
		topic& _m_topic;
	public:
		writer(topic& topic_)
			: _m_topic{topic_}
		{ }
		/**
		 * @brief Publish @p ev to this topic.
		 *
		 * Currently, nobody is responsible for calling `delete` on it, but this will change.
		 */
		virtual void put(const specific_event* this_specific_event) {
			assert(typeid(specific_event) == _m_topic.ty());
			assert(this_specific_event);
			// this new pairs with the delete below for, except for the last time, which pairs with the delete in topic::~topic
			ptr<const event>* this_event = new ptr<const event>{this_specific_event};
			_m_topic.put(this_event);
		}

		/**
		 * @brief Like `new`/`malloc` but more efficient for the specific case.
		 * 
		 * There is an optimization available which has not yet been implemented: switchboard can memory
		 * from old events, like a [slab allocator][1]. Suppose module A publishes data for module
		 * B. B's deallocation through the destructor, and A's allocation through this method completes
		 * the cycle in a [double-buffer (AKA swap-chain)][2].
		 *
		 * [1]: https://en.wikipedia.org/wiki/Slab_allocation
		 * [2]: https://en.wikipedia.org/wiki/Multiple_buffering
		 */
		virtual specific_event* allocate() {
			return new specific_event;
		}
	};

	/**
	 * @brief Schedules the callback @p fn every time an event is published to @p topic_name.
	 *
	 * Switchboard maintains a threadpool to call @p fn. It is possible
	 * multiple instances of @p fn will be running concurrently if the
	 * event's repetition period is less than the runtime of @p fn.
	 *
	 * This is safe to be called from any thread.
	 *
	 * @throws if topic already exists, and its type does not match the @p event.
	 */
	template <typename specific_event>
	void schedule(std::size_t plugin_id, std::string topic_name, std::function<void(ptr<const specific_event>, std::size_t)> fn) {
		get_or_create_topic<event>(topic_name).schedule(plugin_id, topic_name, [=](ptr<const event> this_event, std::size_t it_no) {
			assert(this_event);
			ptr<const specific_event> this_specific_event = std::dynamic_pointer_cast<const specific_event>(this_event);
			assert(this_specific_event);
			fn(this_specific_event, it_no);
		});
	}

	/**
	 * @brief Gets a handle to publish to the topic @p topic_name.
	 *
	 *     // Read topic 3 synchronously
	 *     sb->schedule<topic3_type>("task_1", "topic3", [&](switchboard::ptr<topic3_type>
	 * event3) {
	 *         // This is a lambda expression
	 *         // https://en.cppreference.com/w/cpp/language/lambda
	 *         std::cout << "Got a new event on topic3: " << event3->foo <<
	 * std::endl;
	 *     });
	 *
	 *     while (true) {
	 *         // Read topic 1
	 *         swirchboard::ptr<topic1_type> event1 = topic1.get_latest_ro();
	 *
	 *         // Write to topic 2
	 *         switchboard_ptr<topic2_type> event2 = topic2.allocate();
	 *         event2->foo = 3;
	 *         topic2.put(event2);
	 *     }
	 * }
	 * \endcode
	 *
	 * @throws If topic already exists, and its type does not match the @p event.
	 */
	template <typename specific_event>
	writer<specific_event> get_writer(const std::string& topic_name) {
		return writer<specific_event>{get_or_create_topic<specific_event>(topic_name)};
	}

	/**
	 * @brief Gets a handle to read to the latest value from the topic @p topic_name.
	 *
	 * This is safe to be called from any thread.
	 *
	 * @throws If topic already exists, and its type does not match the @p event.
	 */
	template <typename specific_event>
	writer<specific_event> get_reader(const std::string& topic_name) {
		return reader<specific_event>{get_or_create_topic<specific_event>(topic_name)};
	}

	void stop() {
		const std::lock_guard lock{_m_registry_lock};
		_m_registry.clear();
	}

	~switchboard() {
		stop();
	}

};

}
