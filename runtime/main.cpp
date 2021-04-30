#include <signal.h>
#include <cerrno>
#include "runtime_impl.hpp"
#include "frame_logger2.hpp"

constexpr std::chrono::seconds ILLIXR_RUN_DURATION_DEFAULT {20};

ILLIXR::runtime* r;

[[maybe_unused]] static void signal_handler(int) {
	if (r) {
		r->stop();
	}
}

class cancellable_sleep {
public:
	template <typename T, typename R>
	bool sleep(std::chrono::duration<T, R> duration) {
		auto wake_up_time = std::chrono::system_clock::now() + duration;
		while (!_m_terminate.load() && std::chrono::system_clock::now() < wake_up_time) {
			std::this_thread::sleep_for(std::chrono::milliseconds{100});
		}
		return _m_terminate.load();
	}
	void cancel() {
		_m_terminate.store(true);
	}
private:
	std::atomic<bool> _m_terminate {false};
};



int main(int argc, char* const* argv) {
	setup_frame_logger();
	r = ILLIXR::runtime_factory(nullptr);

	{
		struct sched_param sp = { .sched_priority = 3,};
		[[maybe_unused]] int ret = sched_setscheduler(get_tid(), SCHED_FIFO, &sp);
		if (ret != 0) {
			std::cerr << "My priority is bad" << std::endl;
			abort();
		}
	}

	{
		std::ifstream input_freq {"/sys/devices/system/cpu/cpu0/cpufreq/scaling_max_freq"};
		std::ofstream output_freq {"metrics/frequency"};
		size_t freq;
		input_freq >> freq;
		output_freq << freq;
	}

	[[maybe_unused]] int ret = pthread_setname_np(pthread_self(), "main");
	assert(!ret);

	std::vector<std::string> lib_paths;
	std::transform(argv + 1, argv + argc, std::back_inserter(lib_paths), [](const char* arg) {
		return std::string{arg};
	});
	r->load_so(lib_paths);

	// Two ways of shutting down:
	// Ctrl+C
	// signal(SIGINT, signal_handler);

	// And timer
	std::chrono::seconds run_duration = 
		getenv("ILLIXR_RUN_DURATION")
		? std::chrono::seconds{std::stol(std::string{getenv("ILLIXR_RUN_DURATION")})}
		: ILLIXR_RUN_DURATION_DEFAULT
	;

	cancellable_sleep cs;
	std::thread th{[&]{
		[[maybe_unused]] int ret = pthread_setname_np(pthread_self(), "main_timer");
		assert(!ret);
		cs.sleep(run_duration);
		r->stop();
	}};

	r->wait(); // blocks until shutdown is r->stop()

	// cancel our sleep, so we can join the other thread
	cs.cancel();
	th.join();

	delete r;
	return 0;
}
