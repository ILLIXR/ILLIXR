
#include "phonebook.hpp"
#include "data_format.hpp"
#include <chrono>

namespace ILLIXR {

class realtime_clock : public phonebook::service {
private:
	time_point start;
public:
	realtime_clock()
		: start{now()}
	{ }
	duration time_since_start() const {
		return duration(now() - start);
	}
	time_point now() const {
		return std::chrono::steady_clock::now();
	}
	time_point get_start() const {
		return start;
	}
};
}
