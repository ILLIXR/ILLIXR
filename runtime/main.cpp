#include <signal.h>
#include "runtime_impl.hpp"

ILLIXR::runtime* r;

static void signal_handler(int) {
	if (r) {
		r->stop();
	}
}

int main(int argc, const char * argv[]) {
	signal(SIGINT, signal_handler);

	r = ILLIXR::runtime_factory(nullptr);

	for (int i = 1; i < argc; ++i) {
		r->load_so(argv[i]);
	}
	// r->wait();
	std::this_thread::sleep_for(std::chrono::seconds(60));
	auto start = thread_cpu_time();
	delete r;
	auto stop = thread_cpu_time();
	std::cout << "deleting = " << std::chrono::duration_cast<std::chrono::nanoseconds>(stop - start).count() << "ns" << std::endl;
	return 0;
}
