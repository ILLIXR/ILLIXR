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
	class c_metric_logger : public phonebook::service {
	public:
		/**
		 * @brief Writes one log record.
		 *
		 * See the default record types record_types.hpp. Note that this is an 'open system'; new
		 * log record types can be added in a component according to the instructions in
		 * record_types.hpp.
		 */
		template <typename Record>
		void log(std::unique_ptr<const Record>&& r) {
			log2(&Record::type_descr, std::unique_ptr<const record>(std::move(r)));
		}

		/**
		 * @brief Writes many of the same type of log record.
		 *
		 * This is more efficient than calling log many times.
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

	/**
	 * @brief This class generates unique IDs
	 *
	 * If you need unique IDs (e.g. for each component), have each component call this class through
	 * Phonebook. It returns unique IDs.
	 *
	 * You can use namespaces to express logical containment. The return value will be unique
	 * _between other `get` calls to the same namespace_. This is useful for components and
	 * sub-components. For example, If component with ID 0 has 3 subcomponents, one might call
	 * get(0) to name each of them. Then, suppose component with ID 1 has 2 subcomponents, one might
	 * call get(1) twice to name those. The subcomponent IDs could be reused (non-unique), but tuple
	 * (component ID, subcomponent ID) will still be unique. You can also just use the global
	 * namespace for everything, if you do not care about generating small integers for the IDs.
	 *
	 */
	class c_gen_guid : public phonebook::service {
	public:
		/**
		 * @brief Generate a number, unique from other calls to the same namespace/subnamespace/subsubnamepsace.
		 */
		std::size_t get(std::size_t namespace_ = 0, std::size_t subnamespace = 0, std::size_t subsubnamespace = 0) {
			if (guid_starts[namespace_][subnamespace].count(subsubnamespace) == 0) {
				guid_starts[namespace_][subnamespace][subsubnamespace].store(1);
			}
			return ++guid_starts[namespace_][subnamespace][subsubnamespace];
		}
	private:
		std::unordered_map<std::size_t, std::unordered_map<std::size_t, std::unordered_map<std::size_t, std::atomic<std::size_t>>>> guid_starts;
	};


	static std::chrono::milliseconds LOG_BUFFER_DELAY {1000};

	/**
	 * @brief Coalesces logs of the same type to be written back as a single-transaction.
	 *

	 * In some backend-implementations, logging many logs of the same type is more efficient than
	 * logging them individually; However, the client often wants to produce one log-record at a
	 * time. This class resolves this mismatch by buffering logs from the client. Every time a new
	 * log is added, an internal decision process determines whether or not to flush the buffer, or
	 * keep accumulating and wait unitl later.
	 *
	 * Currently this internal decision process is "is the oldest log in the buffer more than 1
	 * second old?". I chose this because this frequency should have very little overhead, even if
	 * every component is also coalescing at 1 per second.
	 *
	 * At destructor time, any remaining logs are flushed.
	 *
	 * Use like:
	 *
	 * \code{.cpp}
	 * log_coalescer(logger);
	 * lc.log(std::make_unique<const start_skip_iteration_record>(id, it, skip_it));
	 * \endcode
	 *
	 */
	template <typename Record>
	class metric_coalescer {
	private:
		std::shared_ptr<c_metric_logger> logger;
		std::chrono::time_point<std::chrono::high_resolution_clock> last_log;
		std::vector<std::unique_ptr<const Record>> buffer;

	public:
		metric_coalescer(std::shared_ptr<c_metric_logger> logger_)
			: logger{logger_}
			, last_log{std::chrono::high_resolution_clock::now()}
		{ }

		~metric_coalescer() {
			flush();
		}

		/**
		 * @brief Appends a log to the buffer, which will eventually be written.
		 */
		void log(std::unique_ptr<const Record>&& r) {
			buffer.push_back(std::move(r));
			maybe_flush();
		}

		/**
		 * @brief Use internal decision process, and possibly trigger flush.
		 */
		void maybe_flush() {
			if (std::chrono::high_resolution_clock::now() > last_log + LOG_BUFFER_DELAY) {
				flush();
			}
		}

		/**
		 * @brief Flush buffer of logs to the underlying logger.
		 */
		void flush() {
			std::vector<std::unique_ptr<const Record>> buffer2;
			buffer.swap(buffer2);
			logger->log_many(std::move(buffer2));
			last_log = std::chrono::high_resolution_clock::now();
		}
	};
}
