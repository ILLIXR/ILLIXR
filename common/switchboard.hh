#ifndef SWITCHBOARD_HH
#define SWITCHBOARD_HH

#include <iostream>
#include <memory>
#include <functional>

namespace ILLIXR {

template <typename event>
class reader_latest {
public:
	virtual const event* get_latest_ro() const = 0;

	virtual event* get_latest() const = 0;

	virtual ~reader_latest() { };
};

template <typename event>
class writer {
public:
	virtual void put(const event* ev) = 0;

	virtual event* allocate() = 0;
	/* Use this instead of malloc/new, because I can recycle space
	   from old events, like a slab allocator. This completes the
	   cycle in a double-buffer or swap-chain. */

	virtual ~writer() { };
};

/* This class is pure virtual so that I can hide its implementation
   from its users. It will be referenced in components, but
   implemented in the runtime.

   However, virtual methods cannot be templated, so these templated
   methods refer to a virtual method whose type has been erased
   (coerced to/from void*). This is an instance of the Non-Virtual
   Interface pattern:
   https://en.wikibooks.org/wiki/More_C%2B%2B_Idioms/Non-Virtual_Interface
*/
class switchboard {

private:

	virtual
	std::unique_ptr<writer<void>> _p_publish(const std::string& name, std::size_t ty) = 0;

	virtual
	std::unique_ptr<reader_latest<void>> _p_subscribe_latest(const std::string& name, std::size_t ty) = 0;

	virtual
	void _p_schedule(const std::string& name, std::function<void(const void*)> fn, std::size_t ty) = 0;

	/* TODO: (usability) add a method which queries if a topic has a writer. Readers might assert this. */

public:

	template <typename event>
	void schedule(std::string name, std::function<void(const event*)> fn) {
		_p_schedule(name, [=](const void* ptr){ fn(reinterpret_cast<const event*>(ptr)); }, typeid(event).hash_code());
	}
	/* I have opted not to pass the event in here, because I would
	   have to code a different case for read-only, read-copying, and
	   read-consuming. The scheduled function should use
	   subscribe_latest, and then I handle all of those cases in the
	   same place. */

	template <typename event>
	std::unique_ptr<writer<event>> publish(const std::string& name) {
		auto void_writer = _p_publish(name, typeid(event).hash_code());
		return std::move(std::unique_ptr<writer<event>>(reinterpret_cast<writer<event>*>(void_writer.release())));
	}

	template <typename event>
	std::unique_ptr<reader_latest<event>> subscribe_latest(const std::string& name) {
		auto void_writer = _p_subscribe_latest(name, typeid(event).hash_code());
		return std::move(std::unique_ptr<reader_latest<event>>(reinterpret_cast<reader_latest<event>*>(void_writer.release())));
	}
	/* This will only store the *latest* result, no intermediates;
	   Perhaps in the future if there is demand, we could implement
	   reader subscribe(const std::string& name). This reader would
	   have to return all events, not just one. */

	virtual ~switchboard() { }
};

/* TODO: (usability) Do these HAVE to be smart pointers? If the
   copy-constructor is already shallow, they could be concrete
   data-types. */

}

#endif
