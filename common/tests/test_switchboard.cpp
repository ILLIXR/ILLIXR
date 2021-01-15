#include <thread>
#include <random>
#include <cstdint>
#include "gtest/gtest.h"
#include "../switchboard.hpp"

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
	const uint64_t MAX_ITERATIONS = 3;

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

		ASSERT_EQ(sb.get_reader<uint64_wrapper>("multiples_of_six").get_nullable(), nullptr);

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
				switchboard::ptr<const uint64_wrapper> datum = reader.get_nullable();
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
				switchboard::ptr<const uint64_wrapper> datum = reader.get_nullable();
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

		// The last uint64_wrapper is still around because it could be accessed by an async reader
		ASSERT_EQ(uint64_wrapper::get_destructed_count(), MAX_ITERATIONS - 2);
	}
	// I need to end the block here, so switchboard gets destructed
	// Then the last uint64_wrapper's should get destructed

	// Assert destructors get called
	ASSERT_EQ(uint64_wrapper::get_destructed_count(), MAX_ITERATIONS - 1);
}

}
