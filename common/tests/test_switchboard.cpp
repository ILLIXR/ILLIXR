#include <thread>
#include <random>
#include <cstdint>
#include "gtest/gtest.h"
#include "../switchboard.hpp"

namespace ILLIXR {

class SwitchboardTest : public ::testing::Test { };

std::minstd_rand rd;

void short_delay() {
	std::this_thread::sleep_for(std::chrono::milliseconds{std::binomial_distribution<int>{10, 0.5}(rd)});
}

void long_delay() {
	std::this_thread::sleep_for(std::chrono::milliseconds{std::binomial_distribution<int>{20, 0.5}(rd)});
}

TEST_F(SwitchboardTest, TestSyncAsync) {
	typedef switchboard::event_wrapper<uint64_t> uint64_wrapper;
	const uint64_t MAX_ITERATIONS = 100;

	switchboard sb {nullptr};

	uint64_t last_m6p0 = 0;
	uint64_t last_m6p3 = 3;
	sb.schedule<uint64_wrapper>("multiples_of_three", [&](switchboard::ptr<const uint64_wrapper> datum) {
		std::cerr << "callbk-0 " << *datum << std::endl;
		ASSERT_TRUE(*datum % 3 == 0);
		if (*datum % 6 == 0) {
			// assert that we didn't "miss" a value
			ASSERT_TRUE(last_m6p0 + 6 ==  *datum);
			last_m6p0 = *datum;
		} else {
			ASSERT_TRUE((*datum+3) % 6 == 0);
			// assert that we didn't "miss" a value
			ASSERT_TRUE(last_m6p3 + 6 == *datum);
			last_m6p3 = *datum;
		}
	});

	ASSERT_TRUE(!sb.get_reader<uint64_wrapper>("multiples_of_three").get_latest_ro_nullable());

	// seed the topic

	std::thread threads[] {
		std::thread{[&sb] {
			 // Write 6*i to switchboard
			 auto writer = sb.get_writer<uint64_wrapper>("multiples_of_three");
			 for (uint64_t i = 1; i < MAX_ITERATIONS; ++i) {
				 uint64_wrapper* datum = new (writer.allocate()) uint64_wrapper {6*i};
				 std::cerr << "writer-0 " << *datum << std::endl;
			 	 writer.put(datum);
				 long_delay();
			 }
		}},
		std::thread{[&sb] {
			 // Write 6*i+3 to switchboard with a different frequency
			 auto writer = sb.get_writer<uint64_wrapper>("multiples_of_three");
			 for (uint64_t i = 1; i < MAX_ITERATIONS; ++i) {
				 uint64_wrapper* datum = new (writer.allocate()) uint64_wrapper {6*i+3};
				 std::cerr << "writer-1 " << *datum << std::endl;
			 	 writer.put(datum);
				 short_delay();
			 }
		}},
		std::thread{[&sb] {
			 // Reader
			 uint64_t last_m6p0 = 0;
			 uint64_t last_m6p3 = 0;
			 auto reader = sb.get_reader<uint64_wrapper>("multiples_of_three");

			 for (uint64_t i = 0; i < MAX_ITERATIONS; ++i) {
				 if (reader.valid()) {
					 switchboard::ptr<const uint64_wrapper> datum = reader.get_latest_ro();
					 std::cerr << "reader-0 " << *datum << std::endl;
					 ASSERT_TRUE(*datum % 3 == 0);
					 if (*datum % 6 == 0) {
						 // I use leq here because get_latest can return the same thing twice
						 ASSERT_TRUE(last_m6p0 <= *datum);
						 last_m6p0 = *datum;
					 } else {
						 ASSERT_TRUE((*datum + 3) % 6 == 0);
						 ASSERT_TRUE(last_m6p3 <= *datum);
						 last_m6p3 = *datum;
					 }
				 } else {
					 ASSERT_TRUE(!reader.get_latest_ro_nullable());
					 ASSERT_TRUE(last_m6p0 == 0);
					 ASSERT_TRUE(last_m6p3 == 0);
					 std::cerr << "reader-1 not ready yet" << std::endl;
				 }
				 long_delay();
			 }
		}},
		std::thread{[&sb] {
			 // Reader with different frequency
			 uint64_t last_m6p0 = 0;
			 uint64_t last_m6p3 = 0;
			 auto reader = sb.get_reader<uint64_wrapper>("multiples_of_three");

			 for (uint64_t i = 0; i < MAX_ITERATIONS; ++i) {
				 if (reader.valid()) {
					 switchboard::ptr<const uint64_wrapper> datum = reader.get_ro();
					 std::cerr << "reader-1 " << *datum << std::endl;
					 ASSERT_TRUE(*datum % 3 == 0);
					 if (*datum % 6 == 0) {
						 // Async reader, could have "skipped" values
						 // Will check <= instead of ==
						 ASSERT_TRUE(last_m6p0 <= *datum);
						 last_m6p0 = *datum;
					 } else {
						 ASSERT_TRUE((*datum + 3) % 6 == 0);
						 // Async reader, could have "skipped" values
						 // Will check <= instead of ==
						 ASSERT_TRUE(last_m6p3 <= *datum);
						 last_m6p3 = *datum;
					 }
				 } else {
					 ASSERT_TRUE(!reader.get_ro_nullable());
					 ASSERT_TRUE(last_m6p0 == 0);
					 ASSERT_TRUE(last_m6p3 == 0);
					 std::cerr << "reader-1 not ready yet" << std::endl;
				 }
			 }
		}},
	};
	for (std::thread& thread : threads) {
		thread.join();
	}
}

}
