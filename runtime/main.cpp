#include "runtime_impl.hpp"

int main(int argc, const char * argv[]) {
	ILLIXR::runtime* r = ILLIXR::runtime_factory(nullptr);
	for (int i = 1; i < argc; ++i) {
		r->load_so(argv[i]);
	}
	r->wait();
	return 0;
}
