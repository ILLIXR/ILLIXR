#pragma once

#include <atomic>
#include <unordered_map>
#include <any>
#include <cassert>
#include <utility>
#include <vector>
#include <chrono>
#include <ctime>
#include <memory>
#include "phonebook.hpp"
#include "cpu_timer.hpp"

namespace ILLIXR {
	static inline std::chrono::system_clock::time_point __high_res_to_sys_tp(const std::chrono::high_resolution_clock::time_point& input) {
		// https://stackoverflow.com/a/52296562/1078199
		auto highResNow = std::chrono::high_resolution_clock::now();
		auto systemNow = std::chrono::system_clock::now();
		return systemNow + std::chrono::duration_cast<std::chrono::system_clock::duration>(highResNow - input);
	}

	/**
	 * @brief Schema of each record.
	 *
	 * name_ should be globally unique.
	 */
	class record_header {
	public:
		record_header(std::string name_, std::vector<std::pair<std::string, const std::type_info&>> columns_)
			: id{std::hash<std::string>{}(name_)}
			, name{name_}
			, columns{columns_}
		{ }

		bool operator==(const record_header& other) const {
			if (id != other.id) {
				return false;
			}
			if (columns.size() != other.columns.size()) {
				return false;
			}
			for (std::size_t i = 0; i < columns.size(); ++i) {
				if (columns[i] != other.columns[i]) {
					return false;
				}
			}
			return true;
		}
		bool operator!=(const record_header& other) const {
			return !(*this == other);
		}
		std::size_t get_id() const { return id; }
		const std::string& get_name() const { return name; }
		const std::string& get_column_name(unsigned column) const { return columns[column].first; }
		const std::type_info& get_column_type(unsigned column) const { return columns[column].second; }
		unsigned get_columns() const { return columns.size(); }
		std::string to_string() const {
			std::string ret = std::string{"record "} + name + std::string{" ( "};
			for (const auto& pair : columns) {
				ret += std::string{pair.second.name()} + std::string{" "} + pair.first + std::string{"; "};
			}
			ret.erase(ret.size() - 2);
			ret += std::string{" )"};
			return ret;
		}

	private:
		std::size_t id;
		std::string name;
		const std::vector<std::pair<std::string, const std::type_info&>> columns;
	};

	class use_taint {
	public:
		use_taint() : used{false} { }
		use_taint(const use_taint& other) : used{false} {
			other.used = true;
		}
		use_taint& operator=(const use_taint& other) {
			if (&other != this) {
				other.used = true;
				used = false;
				/*
				  class_containing_use_taint a;
				  a.mark_used();
				  // at this point, the data a is tracking is used.

				  a = b;
				  // at this point, the data a is tracking is not yet used.
				  // Therefore a should be marked as unused
				  */
			}
			return *this;
		}
		/*
		  copy constructors are just as efficient as move constructors would be,
		  so I won't define move constructors. C++ will invoke copy instead (for no loss).
		*/
		bool is_used() const { return used; }
		void mark_used() const { used = true; }
		void mark_unused() const { used = false; }
	private:
		mutable bool used;
	};

    class record {
	public:
		record()
			: rh{nullptr}
			, values{}
		{ }

		record(const record_header* rh_, std::vector<std::any> values_)
			: rh{rh_}
			, values{values_}
		{
#ifndef DNDEBUG
			assert(rh);
			if (values.size() != rh->get_columns()) {
				std::cerr << values.size() << " elements passed, but rh for " << rh->get_name() << " only specifies " << rh->get_columns() << "." << std::endl;
				abort();
			}
			for (std::size_t column = 0; column < values.size(); ++column) {
				if (values[column].type() != rh->get_column_type(column)) {
					std::cerr << "Caller got wrong type for column " << column << " of " << rh->get_name() << ". "
							  << "Caller passed:" << values[column].type().name() << "; "
							  << "recod_header for specifies: " << rh->get_column_type(column).name() << ". "
							  << std::endl;
					abort();
				}
			}
#endif
		}

		~record() {
			// assert(rh == nullptr || data_taint.is_used());
		}

		template<typename T>
		T get_value(unsigned column) const {
#ifndef DNDEBUG
			assert(rh);
			data_taint.mark_used();
			if (rh->get_column_type(column) != typeid(T)) {
				std::cerr << "Caller column type for " << column << " of " << rh->get_name() << ". "
						  << "Caller passed: " << typeid(T).name() << "; "
						  << "record_header specifies: " << rh->get_column_type(column).name() << ". "
						  << std::endl;
				abort();
			}
#endif
			return std::any_cast<T>(values[column]);
		}

		const record_header& get_record_header() const {
			assert(rh);
			return *rh;
		}

		std::string to_string() const {
			std::string ret = "record{";
			for (unsigned i = 0; i < values.size(); ++i) {
				if (false) {
				} else if (rh->get_column_type(i) == typeid(std::size_t)) {
					ret += std::to_string(get_value<std::size_t>(i)) + std::string{", "};
				} else if (rh->get_column_type(i) == typeid(std::chrono::nanoseconds)) {
					ret += std::to_string(get_value<std::chrono::nanoseconds>(i).count()) + std::string{"ns, "};
				} else if (rh->get_column_type(i) == typeid(std::chrono::high_resolution_clock::time_point)) {
					std::time_t timet = std::chrono::system_clock::to_time_t(__high_res_to_sys_tp(get_value<std::chrono::high_resolution_clock::time_point>(i)));
					ret += std::string{std::ctime(&timet)} + std::string{", "};
				} else if (rh->get_column_type(i) == typeid(std::string)) {
					ret += std::string{"\""} + get_value<std::string>(i) + std::string{"\", "};
				} else {
					throw std::runtime_error{std::string{"type "} + std::string{rh->get_column_type(i).name()} + std::string{" not implemented"}};
				}
			}
			ret.erase(ret.size() - 2);
			ret += "}";
			return ret;
		}

	private:
		// Holding a pointer to a record_header is more efficient than
		// requiring each record to hold a list of its column names
		// and table name. This is just one pointer.
		const record_header* rh;
		std::vector<std::any> values;
        use_taint data_taint;
    };


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

		virtual ~c_metric_logger() { }

		/**
		 * @brief Writes one log record.
		 */
		virtual void log(const record& r) = 0;

		/**
		 * @brief Writes many of the same type of log record.
		 *
		 * This is more efficient than calling log many times.
		 */
		virtual void log(const std::vector<record>& r) = 0;
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
			return guid_starts[namespace_][subnamespace][subsubnamespace]++;
		}
	private:
		std::unordered_map<std::size_t, std::unordered_map<std::size_t, std::unordered_map<std::size_t, std::atomic<std::size_t>>>> guid_starts;
	};


	static std::chrono::milliseconds LOG_BUFFER_DELAY {1000};

	/**
	 * @brief Coalesces logs of the same type to be written back as a single-transaction.
	 *
	 * Records should all be of the same type.
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
	 * lc.log(make_my_record(id, it, skip_it, ...));
	 * \endcode
	 *
	 */
	class metric_coalescer {
	private:
		std::shared_ptr<c_metric_logger> logger;
		std::chrono::time_point<std::chrono::high_resolution_clock> last_log;
		std::vector<record> buffer;

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
		void log(record r) {
			buffer.push_back(r);
			// Log coalescer should only be used with
			// In the common case, they will be the same pointer, quickly check the pointers.
			// In the less common case, we check for object-structural equality.
#ifndef DNDEBUG
			if (&r.get_record_header() != &buffer[0].get_record_header()
				&& r.get_record_header() == buffer[0].get_record_header()) {
				std::cerr << "Tried to push a record of type " << r.get_record_header().to_string() << " to a metric logger for type " << buffer[0].get_record_header().to_string() << std::endl;
				abort();
			}
#endif
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
			std::vector<record> buffer2;
			buffer.swap(buffer2);
			logger->log(buffer2);
			last_log = std::chrono::high_resolution_clock::now();
		}
	};
}
