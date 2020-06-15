#include "runtime_impl.hpp"

int main(int argc, const char * argv[]) {
	ILLIXR::runtime* r = ILLIXR::runtime_factory(nullptr);
	r->load_so_list(argc, argv);
	r->wait();
	return 0;
}
