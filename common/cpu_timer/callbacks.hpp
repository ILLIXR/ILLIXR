#pragma once // NOLINT(llvm-header-guard)

namespace cpu_timer::detail {
	/*
			void serialize(std::ostream& os, ThreadId thread_id, myclock::WallTime process_start, const char* function_name) const {
			// Emitting [start, (start-stop)] is fewer bytes in CSV than [start, stop].
			// CpuTime already begins at 0, but WallTime gets subtracted from the process start.
			// This makes the time take fewer bytes in CSV, and it makes numbers comparable between runs.
			os
				<< thread_id << ","
				<< frame_id << ","
				<< function_id << ","
				<< (caller != nullptr ? caller->frame_id : 0) << ","
				<< myclock::get_nanoseconds(start_cpu) << ","
				<< myclock::get_nanoseconds(stop_cpu - start_cpu) << ","
				<< myclock::get_nanoseconds(start_wall - process_start) << ","
				<< myclock::get_nanoseconds(stop_wall - start_wall) << ","
				<< function_name << ","
				<< comment << "\n";
		}

		void serialize(std::ostream& os, myclock::WallTime process_start) {
			std::lock_guard<std::mutex> lock{mutex};

			std::vector<bool> function_id_seen (function_id_to_name.size(), false);
			for (const StackFrame& stack_frame : finished) {
				assert(stack_frame.is_timed());
				size_t function_id = stack_frame.get_function_id();
				assert(function_id == function_name_to_id[function_id_to_name[function_id]]);
				if (function_id_seen[function_id]) {
					stack_frame.serialize(os, thread_id, process_start, "");
				} else {
					stack_frame.serialize(os, thread_id, process_start, function_id_to_name[function_id]);
					function_id_seen[function_id] = true;
				}
			}
			finished.clear();
		}

		void serialize(std::ostream& os) {
			std::lock_guard<std::mutex> proc_lock {proc_mutex};
			for (auto [thread_id, stack] : threads_to_stack) {
				stack.serialize(os, thread_id, start_time);
			}
		}

	*/

} // namespace cpu_timer
