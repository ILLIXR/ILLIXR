#include <sched.h>
#include "common/threadloop.hpp"

using namespace ILLIXR;

class dummy : public threadloop {
public:
	dummy(std::string name_, phonebook* pb_)
		: threadloop{name_, pb_}
	{ }

	virtual void _p_thread_setup() {
		cpu_set_t mask;
		CPU_ZERO(&mask);
		CPU_SET(7, &mask);

		[[maybe_unused]] int ret = sched_setaffinity(0, sizeof(mask), &mask);
	}

	virtual void _p_one_iteration() { }
};

PLUGIN_MAIN(dummy);
