#ifndef SWITCHBOARD_HH
#define SWITCHBOARD_HH

#include <iostream>
#include <memory>
#include <functional>
#include "phonebook.hpp"

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

	/* @brief Like `new`/`malloc` but more efficient for the specific case.
	 * 
	 * There is an optimization available which has not yet been implemented: switchboard can memory
	 * from old events, like a [slab allocator][1]. Suppose module A publishes data for module
	 * B. B's deallocation through the destructor, and A's allocation through this method completes
	 * the cycle in a [double-buffer or swap-chain][2].
	 *
	 * [1]: https://en.wikipedia.org/wiki/Slab_allocation
	 * [2]: https://en.wikipedia.org/wiki/Multiple_buffering
	 */
	virtual event* allocate() = 0;

	virtual ~writer() { };
};

/* This class is pure virtual so that I can hide its implementation from its users. It will be
   referenced in components, but implemented in the runtime.

   However, virtual methods cannot be templated, so these templated methods refer to a virtual
   method whose type has been erased (coerced to/from void*). This is an instance of the Non-Virtual
   Interface pattern: https://en.wikibooks.org/wiki/More_C%2B%2B_Idioms/Non-Virtual_Interface
*/

/**
 * @brief A manager for typed, named event-streams (called topics).
 *
 * A topic is identified by its string name.
 */
class switchboard : public service {

private:
	virtual
	std::unique_ptr<writer<void>> _p_publish(const std::string& name, std::size_t ty) = 0;

	virtual
	std::unique_ptr<reader_latest<void>> _p_subscribe_latest(const std::string& name, std::size_t ty) = 0;

	virtual
	void _p_schedule(const std::string& name, std::function<void(const void*)> fn, std::size_t ty) = 0;

	/* TODO: (usability) add a method which queries if a topic has a writer. Readers might assert this. */

public:

	/**
	 * @brief Schedules the callback @p fn every time an event is published to @p name.
	 *
	 * Switchboard maintains a threadpool to call `fn`. It is possible
	 * multiple instances of `fn` will be running concurrently if the
	 * event's repetition period is less than the runtime of `fn`.
	 *
	 * @throws if topic already exists, and its type does not match the `event`.
	 */
	template <typename event>
	void schedule(std::string name, std::function<void(const event*)> fn) {
		_p_schedule(name, [=](const void* ptr){ fn(reinterpret_cast<const event*>(ptr)); }, typeid(event).hash_code());
	}

	/**
	 * @brief Gets a handle to publish to the topic `name`.
	 *
	 * @throws If topic already exists, and its type does not match the `event`.
	 */
	template <typename event>
	std::unique_ptr<writer<event>> publish(const std::string& name) {
		auto void_writer = _p_publish(name, typeid(event).hash_code());
		return std::move(std::unique_ptr<writer<event>>(reinterpret_cast<writer<event>*>(void_writer.release())));
	}

	/**
	 * @brief Gets a handle to read to the latest value from the topic `name`.
	 *
	 * @throws If topic already exists, and its type does not match the `event`.
	 */
	template <typename event>
	std::unique_ptr<reader_latest<event>> subscribe_latest(const std::string& name) {
		auto void_writer = _p_subscribe_latest(name, typeid(event).hash_code());
		return std::move(std::unique_ptr<reader_latest<event>>(reinterpret_cast<reader_latest<event>*>(void_writer.release())));
	}

	virtual ~switchboard() { }
};

/* TODO: (usability) Do these HAVE to be smart pointers? If the
   copy-constructor is already shallow, they could be concrete
   data-types. */

}

#endif
