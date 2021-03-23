#include <sched.h>
#include "common/threadloop.hpp"

using namespace ILLIXR;

class dummy : public plugin {
public:
	dummy(std::string name_, phonebook* pb_)
		: plugin{name_, pb_}
	{ }
};

PLUGIN_MAIN(dummy);
