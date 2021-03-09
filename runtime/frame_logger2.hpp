#pragma once
#include <memory>
#include <string>
#include <unordered_set>
#include <vector>
#include <sys/syscall.h>
#include <sys/types.h>
#include <unistd.h>


#include "common/frame_info.hpp"
#include "common/switchboard.hpp"
#include "sqlite_wrapper.hpp"

namespace ILLIXR {

	pid_t gettid(void) {
		return static_cast<pid_t>(syscall(SYS_gettid));
	}

	static const sqlite::schema frame_schema {{
		{"epoch", sqlite::type_INTEGER},
		{"thread_id", sqlite::type_INTEGER},
		{"frame", sqlite::type_INTEGER},
		{"function_name", sqlite::type_TEXT},
		{"caller", sqlite::type_INTEGER},
		{"file_name", sqlite::type_TEXT},
		{"line", sqlite::type_INTEGER},
		{"wall_start", sqlite::type_INTEGER},
		{"wall_stop", sqlite::type_INTEGER},
		{"cpu_start", sqlite::type_INTEGER},
		{"cpu_stop", sqlite::type_INTEGER},
	}};

	class frame_logger {
	private:
		size_t tid;
		sqlite::database database;
		sqlite::table finished_table;
		// sqlite::table unfinished_table;
		size_t epoch;

		static boost::filesystem::path get_filename(size_t tid) {
			boost::filesystem::path path = (boost::filesystem::path{"metrics"} / "frames" / std::to_string(tid)).replace_extension(".sqlite");
			boost::filesystem::create_directory(path.parent_path());
			std::cerr << path.string() << std::endl;
			assert(boost::filesystem::is_directory(path.parent_path()));
			return path;
		}

	public:
		frame_logger(pid_t tid_)
			: tid{static_cast<size_t>(tid_)}
			, database{get_filename(tid), true}
			, finished_table{database.create_table("finished", frame_schema + site_info_schema)}
			, epoch{0}
		{ }

		void process(cpu_timer::Frames&& finished) {
			const FrameInfo default_frame_info;
			std::vector<std::vector<sqlite::value>> frame_rows;

			cpu_timer::CpuNs start = cpu_timer::detail::cpu_now();
			for (const cpu_timer::Frame& frame : finished) {
				const FrameInfo& info =
					frame.get_info() == cpu_timer::type_eraser_default
					? default_frame_info
					: cpu_timer::extract_type_eraser<FrameInfo>(frame.get_info())
					;
				frame_rows.emplace_back(std::vector<sqlite::value>{
					sqlite::value{epoch},
					sqlite::value{tid},
					sqlite::value{frame.get_index()},
					sqlite::value{std::string_view{frame.get_function_name()}},
					sqlite::value{frame.get_caller_index()},
					sqlite::value{std::string_view{frame.get_file_name()}},
					sqlite::value{frame.get_line()},
					sqlite::value{cpu_timer::detail::get_ns(frame.get_start_wall())},
					sqlite::value{cpu_timer::detail::get_ns(frame.get_stop_wall())},
					sqlite::value{cpu_timer::detail::get_ns(frame.get_start_cpu())},
					sqlite::value{cpu_timer::detail::get_ns(frame.get_stop_cpu())},
				});
				info.serialize(frame_rows.back());
			}
			cpu_timer::CpuNs mid = cpu_timer::detail::cpu_now();
			finished_table.bulk_insert(std::move(frame_rows));
			cpu_timer::CpuNs stop = cpu_timer::detail::cpu_now();

			std::cout
				<< "Stack frame logger on " << tid << ' '
				<< "processed " << finished.size() << " frames, "
				<< "conversion: " << size_t(finished.size() * 1e9 / cpu_timer::detail::get_ns(mid - start)) << "trans per sec, "
				<< "insertion: " << size_t(finished.size() * 1e9 / cpu_timer::detail::get_ns(stop - mid)) << "trans per sec. "
				<< std::endl;
		}
	};

	class frame_logger_container : public cpu_timer::CallbackType {
	private:
		// TODO: do this without a mutex
		std::mutex frame_logger_mutex;
		std::unordered_map<std::thread::id, std::unique_ptr<frame_logger>> frame_loggers;
	protected:
		virtual void thread_start(cpu_timer::Stack& stack) override {
			std::lock_guard<std::mutex> lock {frame_logger_mutex};
			frame_loggers.try_emplace(stack.get_id(), std::make_unique<frame_logger>(stack.get_native_handle()));
		}
		virtual void thread_in_situ(cpu_timer::Stack& stack) override {
			std::lock_guard<std::mutex> lock {frame_logger_mutex};
			frame_loggers.at(stack.get_id())->process(stack.drain_finished());

			// Other in_situ operations should go here.
			// Make a copy of finished or something before I process() it.
			// Could push over Switchboard.
			// However, in the default configuration, stack frames will be held until thread_stop.
		}
		virtual void thread_stop(cpu_timer::Stack& stack) override {
			std::lock_guard<std::mutex> lock {frame_logger_mutex};
			frame_loggers.at(stack.get_id())->process(stack.drain_finished());
		}
	};

	static void setup_frame_logger() {
		cpu_timer::get_process().set_callback(std::make_unique<frame_logger_container>());
		// This makes cpu_timer hold all logs until thread_stop (no in situ).
		// We usually want this behavior for performance reasons.
		cpu_timer::get_process().set_log_period(cpu_timer::CpuNs{0});
		// cpu_timer::get_process().callback_once();
		cpu_timer::get_process().set_enabled(true);
	}
}
