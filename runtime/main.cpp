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
	r->wait();
	return 0;
}
