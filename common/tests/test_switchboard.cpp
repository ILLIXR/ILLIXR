#include <thread>
#include <random>
#include <algorithm>
#include <cstdint>
#include "gtest/gtest.h"
#include "../switchboard.hpp"
#include "../managed_thread.hpp"

namespace ILLIXR {

class SwitchboardTest : public ::testing::Test { };



thread_local std::minstd_rand rd {};
thread_local std::binomial_distribution<long> short_dist{10, 0.5};
void short_delay() {
	//std::this_thread::sleep_for(std::chrono::milliseconds{short_dist(rd)});
}

thread_local std::binomial_distribution<long> long_dist{15, 0.5};
void long_delay() {
	//std::this_thread::sleep_for(std::chrono::milliseconds{long_dist(rd)});
}

class uint64_wrapper : public switchboard::event {
public:
	uint64_wrapper(uint64_wrapper&& other) noexcept
		: _m_default_constructed{other._m_default_constructed}
		, _m_datum{other._m_datum}
	{ }

	uint64_wrapper(uint64_t datum)
		: _m_default_constructed{false}
		, _m_datum{datum}
	{}

	~uint64_wrapper() {
		if (!_m_default_constructed) {
			_s_destructed_count++;
		}
	}

	static std::size_t get_destructed_count() {
		return _s_destructed_count;
	}

	operator uint64_t() const { return _m_datum; }

private:
	bool _m_default_constructed;
	uint64_t _m_datum;
	static std::atomic<std::size_t> _s_destructed_count;
};

std::atomic<std::size_t> uint64_wrapper::_s_destructed_count {0};

TEST_F(SwitchboardTest, TestSyncAsync) {
	const uint64_t MAX_ITERATIONS = 300;

	// I need to start a block here, so the destructor of switchboard gets called
	{
		// Run switchboard without phonebook (and logging)
		switchboard sb {nullptr};

		std::atomic<uint64_t> last_datum_0 = 0;
		std::atomic<uint64_t> last_it_0 = 0;
		std::thread::id callbk_0;

		sb.schedule<uint64_wrapper>(0, "multiples_of_six", [&](switchboard::ptr<const uint64_wrapper>&& datum, std::size_t it) {
			// std::cerr << "callbk-0: " << *datum << std::endl;
			// Assert we are on our own thread
			if (last_it_0 == 0) {
				callbk_0 = std::this_thread::get_id();
			} else {
				ASSERT_EQ(callbk_0, std::this_thread::get_id());
			}

			short_delay();

			// Assert we didn't "miss" any values

			ASSERT_EQ(last_it_0 + 1, it);
			last_it_0 = it;

			ASSERT_EQ(last_datum_0 + 6,  *datum);
			last_datum_0 = *datum;
		});

		std::atomic<uint64_t> last_datum_1 = 0;
		std::atomic<uint64_t> last_it_1 = 0;
		std::thread::id callbk_1;

		sb.schedule<uint64_wrapper>(1, "multiples_of_six", [&](switchboard::ptr<const uint64_wrapper>&& datum, std::size_t it) {
			// std::cerr << "callbk-1: " << *datum << std::endl;
			// Assert we are on our own thread
			if (last_it_1 == 0) {
				callbk_1 = std::this_thread::get_id();
			} else {
				ASSERT_EQ(callbk_1, std::this_thread::get_id());
			}

			long_delay();

			// Assert we didn't "miss" any values
			// Despite being much slower than our other reader
			// This also stresses memory correctness

			ASSERT_EQ(last_it_1 + 1, it);
			last_it_1 = it;

			ASSERT_EQ(last_datum_1 + 6,  *datum);
			last_datum_1 = *datum;
		});

		ASSERT_EQ(sb.get_reader<uint64_wrapper>("multiples_of_six").get_ro_nullable(), nullptr);

		std::thread writer {[&sb] {
			auto writer = sb.get_writer<uint64_wrapper>("multiples_of_six");
			for (uint64_t i = 1; i < MAX_ITERATIONS; ++i) {
				uint64_wrapper* datum = new (writer.allocate()) uint64_wrapper {6*i};
				// std::cerr << "writer-0: " << *datum << std::endl;
				writer.put(datum);
				long_delay();
			}
		}};

		std::thread fast_reader {[&sb] {
			uint64_t last_datum = 0;
			auto reader = sb.get_reader<uint64_wrapper>("multiples_of_six");

			for (uint64_t i = 0; i < MAX_ITERATIONS; ++i) {
				switchboard::ptr<const uint64_wrapper> datum = reader.get_ro_nullable();
				if (!datum) {
					// Nothing on topic yet
					// Assert this only happens in the beginning
					// std::cerr << "reader-1: null" << std::endl;
					ASSERT_EQ(last_datum, 0);
				} else {
					// std::cerr << "reader-1: " << *datum << std::endl;
					// I use leq here because get_latest can return the same thing twice
					ASSERT_LE(last_datum, *datum);
					ASSERT_EQ(*datum % 6, 0);
					last_datum = *datum;
				}
				short_delay();
			}
		}};
		std::thread slow_reader {[&sb] {
			uint64_t last_datum = 0;
			auto reader = sb.get_reader<uint64_wrapper>("multiples_of_six");

			for (uint64_t i = 0; i < MAX_ITERATIONS; ++i) {
				switchboard::ptr<const uint64_wrapper> datum = reader.get_ro_nullable();
				if (!datum) {
					// Nothing on topic yet
					// Assert this only happens in the beginning
					// std::cerr << "reader-1: null" << std::endl;
					ASSERT_EQ(last_datum, 0);
				} else {
					// std::cerr << "reader-1: " << *datum << std::endl;
					// I use leq here because get_latest can return the same thing twice
					ASSERT_LE(last_datum, *datum);
					ASSERT_EQ(*datum % 6, 0);
					last_datum = *datum;
				}
				long_delay();
			}
		}};

		writer.join();
		fast_reader.join();
		slow_reader.join();

		while (last_it_0 != MAX_ITERATIONS - 1 || last_it_1 != MAX_ITERATIONS - 1) {
		    // TODO: Configure with Issue #94 when merged
			std::this_thread::sleep_for(std::chrono::milliseconds{100});
		}

		// Assert both of callbacks have run on all inputs
		ASSERT_EQ(last_datum_0, (MAX_ITERATIONS - 1) * 6);
		ASSERT_EQ(last_datum_1, (MAX_ITERATIONS - 1) * 6);

		// The last few events are still around because it is in the latest buffer
	}
	// I need to end the block here, so switchboard gets destructed
	// Then the last uint64_wrapper's should get destructed

	// Assert destructors get called
	ASSERT_EQ(uint64_wrapper::get_destructed_count(), MAX_ITERATIONS - 1);
}

void sieve(int n) {
		    // Create a boolean array 
		    // "prime[0..n]" and initialize
		    // all entries it as true. 
		    // A value in prime[i] will
		    // finally be false if i is 
		    // Not a prime, else true.
		    bool prime[n + 1];
		    memset(prime, true, sizeof(prime));
		 
		    for (int p = 2; p * p <= n; p++)
		    {
		        // If prime[p] is not changed, 
		        // then it is a prime
		        if (prime[p] == true) 
		        {
		            // Update all multiples 
		            // of p greater than or
		            // equal to the square of it
		            // numbers which are multiple 
		            // of p and are less than p^2 
		            // are already been marked.
		            for (int i = p * p; i <= n; i += p)
		                prime[i] = false;
		        }
		    }
		 
		    // Print all prime numbers
		    for (int p = 2; p <= n; p++)
		        if (prime[p])
					(void)p;
}

TEST_F(SwitchboardTest, TestLatency) {
	// Run switchboard without phonebook (and logging)

	using event = switchboard::event_wrapper<std::chrono::system_clock::time_point>;
	using ns = std::chrono::nanoseconds;
	using ms = std::chrono::milliseconds;
	constexpr size_t N_PERIODS = 8;
	constexpr auto MAX_PERIOD = ns{ms{512}};

	auto csv = std::ofstream{"latency.csv"};
	csv << "period,latency\n";

	for (size_t i = 0; i < N_PERIODS; ++i) {
		switchboard sb {nullptr};

		auto period = MAX_PERIOD / (2 << i);
		auto iters = std::max(size_t(ns{ms{200}} / period), size_t(5));

		std::array<managed_thread*, 20> busy_threads;
		for (int j = 0; j < 20; ++j) {
			busy_threads.at(j) = new managed_thread{[] {
				sieve(1000);
			}};
			busy_threads.at(j)->start();
		}

		const managed_thread& reader = sb.schedule<event>(0, "data", [&](switchboard::ptr<const event>&& datum, std::size_t) {
			csv << ns{period}.count() << ',' << ns{std::chrono::system_clock::now() - **datum}.count() << '\n';
		});
		reader.set_priority(4);

		Event writer_done;
		managed_thread writer {[&] {
			auto writer = sb.get_writer<event>("data");
			for (uint64_t i = 1; i < iters; ++i) {
				writer.put(writer.allocate(std::chrono::system_clock::now()));
				std::this_thread::sleep_for(period);
			}
			writer_done.set();
		}};
		writer.start();
		writer.set_priority(4);
		writer_done.wait();

		for (managed_thread* thread : busy_threads) {
			thread->stop();
			delete thread;
		}
	}
}

}
