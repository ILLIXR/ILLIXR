#include "gtest/gtest.h"
#include "include/cpu_timer.hpp"
#include <algorithm>
#include <deque>
#include <mutex>
#include <ostream>
#include <unordered_map>

void trace1() {
	CPU_TIMER_TIME_FUNCTION();
	// test crossing object-file boundary
	void trace2();
	trace2();
}

void verify_thread_main(const cpu_timer::Frames&, const cpu_timer::Frame& frame) {
	EXPECT_EQ(0, frame.get_caller_index());
	EXPECT_EQ(0, frame.get_index());
	EXPECT_EQ(nullptr, frame.get_function_name());
}

void verify_preorder(const cpu_timer::Frames& trace) {
	// trace should already be in preorder
	auto preorder_trace = cpu_timer::Frames{trace.cbegin(), trace.cend()};
	std::sort(preorder_trace.begin(), preorder_trace.end(), [](const cpu_timer::Frame& f1, const cpu_timer::Frame& f2) {
		return f1.get_index() < f2.get_index();
	});

	for (const cpu_timer::Frame& frame : preorder_trace) {
		std::cout << frame;
	}

	verify_thread_main(preorder_trace, preorder_trace.at(0));
	for (size_t i = 0; i < preorder_trace.size(); ++i) {
		auto& frame = preorder_trace.at(i);
		// All `index`es from 0..n should be used, so the sort should be "dense"
		EXPECT_EQ(frame.get_index(), i);

		// Check that children are refer to the same parent.
		if (!frame.is_leaf()) {
			size_t child_index = frame.get_youngest_callee_index();
			const cpu_timer::Frame* child = nullptr;
			do {
				child =  &preorder_trace.at(child_index);
				EXPECT_EQ(child->get_caller_index(), frame.get_index());
				child_index = child->get_prev_index();
			} while (child->has_prev());
		}

		if (i > 0) {
			auto& prev_frame = preorder_trace.at(i-1);
			// In preorder, prior frames started earlier

			if (frame.get_start_cpu() != cpu_timer::CpuNs{0}) {
				EXPECT_LT(prev_frame.get_start_cpu (), frame.get_start_cpu ());
			}
			if (frame.get_start_wall() != cpu_timer::WallNs{0}) {
				EXPECT_LT(prev_frame.get_start_wall(), frame.get_start_wall());
			}

			// Check if sorted
			EXPECT_EQ(prev_frame.get_index(), frame.get_index() - 1);

			// Our caller must have started before us, except for thread_main, which is a loop.
			EXPECT_LT(frame.get_caller_index(), frame.get_index());
			// This also proves that the trace digraph is a tree rooted at the frames[0] record.
			// Assume for induction i > 0 and frames[0:i] are reachable from the frames[0] record.
			// If frames[i]'s parent is one of frames[0:i], then frames[i] is also reachable from the 0th record, the inductive step.
			// Base case is that frames[0] is in the tree rooted at frames[0] (tautology).

			// This asserts that this frame is on the linked-list pointed to by its parent.
			size_t sibling_index = preorder_trace.at(frame.get_caller_index()).get_youngest_callee_index();
			const cpu_timer::Frame* sibling = nullptr;
			bool found = false;
			do {
				sibling = &preorder_trace.at(sibling_index);
				if (sibling == &frame) {
					found = true;
					break;
				}
				sibling_index = sibling->get_prev_index();
			} while (sibling->has_prev());
			EXPECT_TRUE(found);
		}
	}
}

void verify_postorder(const cpu_timer::Frames& postorder_trace) {
	// auto postorder_trace = cpu_timer::Frames{trace.cbegin(), trace.cend()};
	// std::sort(postorder_trace.begin(), postorder_trace.end(), [](const Frame& f1, const Frame& f2) {
	// 	return f1.get_stop_index() < f2.get_stop_index();
	// });
	verify_thread_main(postorder_trace, postorder_trace.at(postorder_trace.size() - 1));
	for (size_t i = 0; i < postorder_trace.size(); ++i) {
		auto frame = postorder_trace.at(i);

		if (i > 0) {
			auto prev_frame = postorder_trace.at(i-1);

			// In postorder, prior frames finished earlier
			if (frame.get_stop_cpu() != cpu_timer::CpuNs{0}) {
				EXPECT_LT(prev_frame.get_stop_cpu (), frame.get_stop_cpu ());
			}
			if (frame.get_stop_wall() != cpu_timer::WallNs{0}) {
				EXPECT_LT(prev_frame.get_stop_wall(), frame.get_stop_wall());
			}
		}
	}
}

void verify_general(const cpu_timer::Frames& trace) {
	verify_preorder (trace);
	verify_postorder(trace);
}

void verify_trace1(const cpu_timer::Frames& trace) {
	EXPECT_NE(trace.at(0).get_line(), trace.at(1).get_line());
	EXPECT_EQ(std::string{"trace4"}, trace.at(0).get_function_name());
	EXPECT_EQ(2                    , trace.at(0).get_caller_index());
	EXPECT_EQ(std::string{"trace4"}, trace.at(1).get_function_name());
	EXPECT_EQ(2                    , trace.at(1).get_caller_index());
	EXPECT_EQ(std::string{"trace2"}, trace.at(2).get_function_name());
	EXPECT_EQ(std::string{"hello"} , cpu_timer::extract_type_eraser<std::string>(trace.at(2).get_info()));
	EXPECT_EQ(1                    , trace.at(2).get_caller_index());
	EXPECT_EQ(std::string{"trace1"}, trace.at(3).get_function_name());
	EXPECT_EQ(0                    , trace.at(3).get_caller_index());
	EXPECT_EQ(nullptr              , trace.at(4).get_function_name());
	EXPECT_EQ(0                    , trace.at(4).get_caller_index());
}

void verify_trace3(cpu_timer::Frames trace) {
	EXPECT_NE(trace.at(0).get_line(), trace.at(1).get_line());
	EXPECT_EQ(std::string{"trace4"}, trace.at(0).get_function_name());
	EXPECT_EQ(1                    , trace.at(0).get_caller_index());
	EXPECT_EQ(std::string{"trace4"}, trace.at(1).get_function_name());
	EXPECT_EQ(1                    , trace.at(1).get_caller_index());
	EXPECT_EQ(std::string{"trace3"}, trace.at(2).get_function_name());
	EXPECT_EQ(0                    , trace.at(2).get_caller_index());
	EXPECT_EQ(nullptr              , trace.at(3).get_function_name());
	EXPECT_EQ(0                    , trace.at(3).get_caller_index());
}

void err_callback(const cpu_timer::Stack&, cpu_timer::Frames&&, const cpu_timer::Frames&) {
	ADD_FAILURE();
}

class Globals {
public:
	Globals() noexcept {
		auto& proc = cpu_timer::get_process();
		proc.set_enabled(true);
		proc.callback_every_frame();
		proc.set_callback(&err_callback);
	}
	~Globals() {
		cpu_timer::get_process().set_callback(cpu_timer::CallbackType{});
	}
	Globals(const Globals&) = delete;
	Globals& operator=(const Globals&) = delete;
	Globals(const Globals&&) = delete;
	Globals& operator=(const Globals&&) = delete;
};

static Globals globals;

void verify_any_trace(const cpu_timer::Frames& trace) {
	verify_general(trace);
	if (trace.at(trace.size() - 2).get_function_name() == std::string{"trace1"}) {
		verify_trace1(trace);
	} else {
		verify_trace3(trace);
	}
}


// NOLINTNEXTLINE(hicpp-special-member-functions,cppcoreguidelines-special-member-functions,cppcoreguidelines-owning-memory,cert-err58-cpp,misc-unused-parameters)
TEST(CpuTimerTest, TraceCorrectBatched) {
	cpu_timer::get_process().callback_once();
	cpu_timer::get_process().set_callback([=](const cpu_timer::Stack&, cpu_timer::Frames&& finished, const cpu_timer::Frames& stack) {
		EXPECT_EQ(0, stack.size());
		verify_any_trace(finished);
	});
	std::thread th {trace1};
	th.join();
	cpu_timer::get_process().flush();
	cpu_timer::get_process().set_callback(&err_callback);
}

// NOLINTNEXTLINE(hicpp-special-member-functions,cppcoreguidelines-special-member-functions,cppcoreguidelines-owning-memory,cert-err58-cpp,misc-unused-parameters)
TEST(CpuTimerTest, TraceCorrectUnbatched) {
	std::mutex mutex;
	std::unordered_map<std::thread::id, size_t> count;
	std::unordered_map<std::thread::id, cpu_timer::Frames> accumulated;
	cpu_timer::get_process().callback_every_frame();
	cpu_timer::get_process().set_callback([&](const cpu_timer::Stack& stack, cpu_timer::Frames&& finished, const cpu_timer::Frames&) {
		auto thread = stack.get_id();
		std::lock_guard<std::mutex> lock{mutex};
		EXPECT_EQ(finished.size(), 1);
		count[thread]++;
		accumulated[thread].insert(accumulated[thread].end(), finished.cbegin(), finished.cend());
	});
	std::thread th {trace1};
	th.join();
	cpu_timer::get_process().set_callback(&err_callback);

	for (const auto& pair : accumulated) {
		verify_any_trace(pair.second);
		if (pair.second.at(pair.second.size() - 2).get_function_name() == std::string{"trace1"}) {
			EXPECT_EQ(count.at(pair.first), 5);
		} else {
			EXPECT_EQ(count.at(pair.first), 4);
		}
	}
}
