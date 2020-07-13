#pragma once

#include <atomic>
#include <unordered_map>
#include <vector>
#include <chrono>
#include <memory>
#include "phonebook.hpp"
#include "cpu_timer.hpp"
#include "record_types.hpp"

namespace ILLIXR {

	/**
	 * @brief The ILLIXR logging service.
	 *
	 * This has two advantages over printf logging. It has lower
	 * overhead (because it goes into a database), won't result in
	 * spliced messages (no stdout race-conditions), and is used
	 * uniformly by ILLIXR components.
	 */
	class c_logger : public phonebook::service {
	public:
		/**
		 * @brief Writes one log record.
		 *
		 * Record must be in record_types.hpp
		 */
		template <typename Record>
		void log(std::unique_ptr<const Record>&& r) {
			log2(&Record::type_descr, std::unique_ptr<const record>(std::move(r)));
		}

		/**
		 * @brief Writes many of the same type of log record.
		 *
		 * This is more efficient than calling log many times.
		 *
		 * Record must be in record_types.hpp
		 */
		template <typename Record>
		void log_many(std::vector<std::unique_ptr<const Record>>&& rs) {
			std::vector<std::unique_ptr<const record>> rs2;
			for (std::unique_ptr<const Record>& r : rs) {
				rs2.emplace_back(std::move(r));
			}
			// I think (hope) the compiler will optimize this copy-and-cast away.
			// Unfortunately, there is no way to cast every element of a vector all at once according to
			// https://stackoverflow.com/a/20310090/1078199
			log_many2(&Record::type_descr, std::move(rs2));
		}

	protected:
		virtual void log2(const struct_type* ty, std::unique_ptr<const record>&& r) = 0;
		virtual void log_many2(const struct_type* ty, std::vector<std::unique_ptr<const record>>&& r) = 0;
	};

	class c_gen_guid : public phonebook::service {
	public:
		std::size_t get(std::size_t namespace_ = 0, std::size_t subnamespace = 0, std::size_t subsubnamespace = 0) {
			return ++guid_starts[namespace_][subnamespace][subsubnamespace];
		}
	private:
		std::unordered_map<std::size_t, std::unordered_map<std::size_t, std::unordered_map<std::size_t, std::atomic<std::size_t>>>> guid_starts;
	};


	static std::chrono::milliseconds LOG_BUFFER_DELAY {1000};

	template <typename Record>
	class log_coalescer {
	private:
		std::shared_ptr<c_logger> logger;
		std::chrono::time_point<std::chrono::high_resolution_clock> last_log;
		std::vector<std::unique_ptr<const Record>> buffer;
	public:
		log_coalescer(std::shared_ptr<c_logger> logger_)
			: logger{logger_}
			, last_log{std::chrono::high_resolution_clock::now()}
		{ }
		~log_coalescer() {
			flush();
		}
		void log(std::unique_ptr<const Record>&& r) {
			buffer.push_back(std::move(r));
			maybe_flush();
		}
		void maybe_flush() {
			if (std::chrono::high_resolution_clock::now() > last_log + LOG_BUFFER_DELAY) {
				flush();
			}
		}
		void flush() {
			std::vector<std::unique_ptr<const Record>> buffer2;
			buffer.swap(buffer2);
			logger->log_many(std::move(buffer2));
			last_log = std::chrono::high_resolution_clock::now();
		}
	};

}
