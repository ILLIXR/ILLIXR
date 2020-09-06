#ifndef SWITCHBOARD_HH
#define SWITCHBOARD_HH

#include <iostream>
#include <memory>
#include <functional>
#include "phonebook.hpp"
#include "cpu_timer.hpp"

namespace ILLIXR {

/**
 * @brief A handle which can read the latest event on a topic.
 */
template <typename event>
class reader_latest {
public:
	/**
	 * @brief Gets a "read-only" copy of the latest value.
	 */
	virtual const event* get_latest_ro() const = 0;

	/**
	 * @brief Gets a mutable copy of the latest value.
	 */
	virtual event* get_latest() const = 0;

	virtual ~reader_latest() { };
};

/**
 * @brief A handle which can publish events to a topic.
 */
template <typename event>
class writer {
public:
	/**
	 * @brief Publish @p ev to this topic.
	 *
	 * Currently, nobody is responsible for calling `delete` on it, but this will change.
	 */
	virtual void put(const event* ev) = 0;

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
	virtual event* allocate() = 0;

	virtual ~writer() { };
};

/* This class is pure virtual so that I can hide its implementation from its users. It will be
   referenced in plugins, but implemented in the runtime.

   However, virtual methods cannot be templated, so these templated methods refer to a virtual
   method whose type has been erased (coerced to/from void*). This is an instance of the Non-Virtual
   Interface pattern: https://en.wikibooks.org/wiki/More_C%2B%2B_Idioms/Non-Virtual_Interface
*/

/**
 * @brief A manager for typesafe, threadsafe, named event-streams (called topics).
 *
 * - Writing: One can write to a topic (in any thread) through the `ILLIXR::writer` returned by
 *   `publish()`.
 * 
 * - There are two ways of reading: asynchronous reading and synchronous reading:
 *
 *   - Asynchronous reading returns the most-recent event on the topic (idempotently). One can do
 *     this through (in any thread) the `ILLIXR::reader_latest` handle returned by
 *     `subscribe_latest()`.
 *
 *   - Synchronous reading schedules a callback to be executed on _every_ event which gets
 *     published. One can schedule computation by `schedule()`, which will run the computation in a
 *     thread managed by switchboard.
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
 *
 */
class switchboard : public phonebook::service {

private:
	virtual
	std::unique_ptr<writer<void>> _p_publish(const std::string& topic_name, std::size_t ty) = 0;

	virtual
	std::unique_ptr<reader_latest<void>> _p_subscribe_latest(const std::string& topic_name, std::size_t ty) = 0;

	virtual
	void _p_schedule(const std::string& topic_name, std::function<void(const void*)> fn, std::size_t ty) = 0;

	/* TODO: (usability) add a method which queries if a topic has a writer. Readers might assert this. */

public:

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
	template <typename event>
	void schedule([[maybe_unused]] std::string account_name, std::string topic_name, std::function<void(const event*)> fn) {
		_p_schedule(topic_name, [=](const void* ptr) {
			fn(reinterpret_cast<const event*>(ptr));
		}, typeid(event).hash_code());
	}

	/**
	 * @brief Gets a handle to publish to the topic @p topic_name.
	 *
	 * This is safe to be called from any thread.
	 *
	 * @throws If topic already exists, and its type does not match the @p event.
	 */
	template <typename event>
	std::unique_ptr<writer<event>> publish(const std::string& topic_name) {
		auto void_writer = _p_publish(topic_name, typeid(event).hash_code());
		return std::move(std::unique_ptr<writer<event>>(reinterpret_cast<writer<event>*>(void_writer.release())));
	}

	/**
	 * @brief Gets a handle to read to the latest value from the topic @p topic_name.
	 *
	 * This is safe to be called from any thread.
	 *
	 * @throws If topic already exists, and its type does not match the @p event.
	 */
	template <typename event>
	std::unique_ptr<reader_latest<event>> subscribe_latest(const std::string& topic_name) {
		auto void_writer = _p_subscribe_latest(topic_name, typeid(event).hash_code());
		return std::move(std::unique_ptr<reader_latest<event>>(reinterpret_cast<reader_latest<event>*>(void_writer.release())));
	}

	virtual ~switchboard() { }

	virtual void stop() = 0;
};

/* TODO: (usability) Do these HAVE to be smart pointers? If the
   copy-constructor is already shallow, they could be concrete
   data-types. */

}

#endif
