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
	class logger : public phonebook::service {
	public:
		/**
		 * @brief Writes one log record.
		 *
		 * Record must be in record_types.hpp
		 */
		template <typename Record>
		void log([[maybe_unused]] Record r) {
			log(Record::id, reinterpret_cast<const char*>(static_cast<const void*>(&r)));
		}

		/**
		 * @brief Writes many of the same type of log record.
		 *
		 * This is more efficient than calling log many times.
		 *
		 * Record must be in record_types.hpp
		 */
		template <typename Record>
		void log_many([[maybe_unused]] std::unique_ptr<std::vector<Record>> rs) {
			log_many(Record::id, rs->size(), reinterpret_cast<const char*>(reinterpret_cast<const void*>(&*rs->cbegin())));
		}

	protected:
		virtual void log(record_type_id ty, const char* r) = 0;
		virtual void log_many(record_type_id ty, std::size_t sz, const char* rbegin) = 0;
	};

	class gen_guid : public phonebook::service {
	public:
		std::size_t get(std::size_t namespace_ = 0, std::size_t subnamespace = 0, std::size_t subsubnamespace = 0) {
			return ++guid_starts[namespace_][subnamespace][subsubnamespace];
		}
	private:
		std::unordered_map<std::size_t, std::unordered_map<std::size_t, std::unordered_map<std::size_t, std::atomic<std::size_t>>>> guid_starts;
	};


	static std::chrono::milliseconds LOG_BUFFER_DELAY {1000};

	typedef logger c_logger;
	typedef gen_guid c_gen_guid;

	template <typename Record>
	class log_coalescer {
	private:
		std::shared_ptr<logger> logger;
		std::chrono::time_point<std::chrono::high_resolution_clock> last_log;
		std::unique_ptr<std::vector<Record>> buffer;
	public:
		log_coalescer(std::shared_ptr<c_logger> logger_)
			: logger{logger_}
			, last_log{std::chrono::high_resolution_clock::now()}
			, buffer{std::make_unique<std::vector<Record>>()}
		{ }
		~log_coalescer() {
			flush();
		}
		void log(Record r) {
			buffer->push_back(r);
			maybe_flush();
		}
		void maybe_flush() {
			if (std::chrono::high_resolution_clock::now() > last_log + LOG_BUFFER_DELAY) {
				flush();
			}
		}
		void flush() {
			std::unique_ptr<std::vector<Record>> buffer2 {std::make_unique<std::vector<Record>>()};
			buffer.swap(buffer2);
			logger->log_many(std::move(buffer2));
			last_log = std::chrono::high_resolution_clock::now();
		}
	};

}
