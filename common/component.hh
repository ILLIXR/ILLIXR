#ifndef ABSTRACT_COMPONENT_HH
#define ABSTRACT_COMPONENT_HH

#include <atomic>
#include <future>
#include "switchboard.hh"

namespace ILLIXR {

class component {
public:

	/* I am using the Non-Virtual Interface pattern, so that
	   validation and logging can be centralized here. See
	   https://en.wikibooks.org/wiki/More_C%2B%2B_Idioms/Non-Virtual_Interface
	*/
	void start() { _p_start(); }
	void stop() { _p_stop(); }
	void compute_one_iteration() { _p_compute_one_iteration(); }

	virtual ~component() { }

private:
	/* You may choose to override _p_start() and completely control
	   the concurrent thread loop. However, leaving this default will
	   help the RT optimally schedule your computation. */
	virtual void _p_start() {
		_m_thread = std::thread([this]() {
			while (!_m_terminate.load()) {
				compute_one_iteration();
			}
		});
		/* TODO: (performance) This is better phrased as:
            switchboard.schedule("always", component::compute_one_iteration)
		which would utilize switchboard's threadpool, instead of creating a new thread here.
		It also allows user-level scheduling of this component.
		*/
	}

	virtual void _p_stop() {
		_m_terminate.store(true);
		_m_thread.join();
	}

	/* If you use the default _p_start/_p_stop, put your computation
	   in here. If you override them, you can safely ignore this. */
	virtual void _p_compute_one_iteration() { }

	std::atomic<bool> _m_terminate {false};
	std::thread _m_thread;
};

/*
Define a new type, which will be a function type.
        [ret type]   [---type name-----]   [----arg type----] */
typedef component* (*create_component_fn) (const switchboard*);

/*
Component SO's must define the following method

    extern "C" component* create_component(switchboard* sb) {
        // ...
    }

 */


}

#endif
