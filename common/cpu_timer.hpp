#include <string>
#include <chrono>
#include <ratio>
#include <cerrno>
#include <cstring>
#include <ctime>

std::chrono::nanoseconds
cpp_clock_gettime() {
	struct timespec ts;
	if (clock_gettime(CLOCK_THREAD_CPUTIME_ID, &ts)) {
		throw std::runtime_error{std::string{"clock_gettime returned "} + strerror(errno)};
	}
	return std::chrono::seconds{ts.tv_sec} + std::chrono::nanoseconds{ts.tv_nsec};
}

class time_this_block_obj {
public:
	time_this_block_obj(std::chrono::nanoseconds& duration_)
		: duration{duration_}
	{
		start = cpp_clock_gettime();
	}
	~time_this_block_obj() {
		duration = cpp_clock_gettime() - start;
	}
private:
	std::chrono::nanoseconds& duration;
	std::chrono::nanoseconds start;
};


#define TIME_THIS_BLOCK(duration) time_this_block_obj _ {duration};
