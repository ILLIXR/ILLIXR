#pragma once

#include "phonebook.hpp"

#include <any>
#include <atomic>
#include <cassert>
#include <chrono>
#include <memory>
#include <optional>
#include <spdlog/spdlog.h>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#ifndef NDEBUG
    #include <iostream>
    #include <sstream>
#endif

namespace ILLIXR {

/**
 * @brief Schema of each record.
 *
 * name_ should be globally unique.
 */
class record_header {
public:
    record_header(const std::string& name, std::vector<std::pair<std::string, const std::type_info&>> columns_)
        : id_{std::hash<std::string>{}(name)}
        , name_{name}
        , columns_{std::move(columns_)} { }

    /**
     * @brief Compares two schemata.
     */
    bool operator==(const record_header& other) const {
        // Check pointer first
        if (this == &other) {
            return true;
        }

        if (name_ != other.name_ || columns_.size() != other.columns_.size() || id_ != other.id_) {
            return false;
        }
        for (std::size_t i = 0; i < columns_.size(); ++i) {
            if (columns_[i] != other.columns_[i]) {
                return false;
            }
        }
        return true;
    }

    bool operator!=(const record_header& other) const {
        return !(*this == other);
    }

    [[nodiscard]] std::size_t get_id() const {
        return id_;
    }

    [[nodiscard]] const std::string& get_name() const {
        return name_;
    }

    [[nodiscard]] const std::string& get_column_name(unsigned column) const {
        return columns_[column].first;
    }

    [[nodiscard]] const std::type_info& get_column_type(unsigned column) const {
        return columns_[column].second;
    }

    [[nodiscard]] size_t get_columns() const {
        return columns_.size();
    }

    [[nodiscard]] std::string to_string() const {
        std::string ret = std::string{"record_header "} + name_ + std::string{" { "};
        for (const auto& pair : columns_) {
            ret += std::string{pair.second.name()} + std::string{" "} + pair.first + std::string{"; "};
        }
        ret.erase(ret.size() - 2);
        ret += std::string{" }"};
        return ret;
    }

private:
    std::size_t                                                      id_;
    std::string                                                      name_;
    const std::vector<std::pair<std::string, const std::type_info&>> columns_;
};

/**
 * @brief A helper class that lets one dynamically determine if some data gets used.
 *
 * When a data_use_indicator gets copied, the original is considered used and the new one is considered unused.
 */
class data_use_indicator {
public:
    data_use_indicator()
        : used_{false} { }

    data_use_indicator(const data_use_indicator& other)
        : used_{false} {
        other.used_ = true;
    }

    data_use_indicator& operator=(const data_use_indicator& other) {
        if (&other != this) {
            other.used_ = true;
            used_       = false;
        }
        return *this;
    }

    /*
      copy constructors are just as efficient as move constructors would be,
      so I won't define move constructors. C++ will invoke copy instead (for no loss).
    */
    bool is_used() const {
        return used_;
    }

    void mark_used() const {
        used_ = true;
    }

    [[maybe_unused]] void mark_unused() const {
        used_ = false;
    }

private:
    mutable bool used_;
};

/**
 * @brief This class represents a tuple of fields which get logged by `record_logger`.
 *
 * `rh` is a pointer rather than a reference for historical reasons. It should not be null.
 */
class record {
public:
    record(const record_header& rh, std::vector<std::any> values)
        : record_header_{rh}
        , values_(std::move(values)) {
#ifndef NDEBUG
        assert(record_header_);
        if (values_.size() != record_header_->get().get_columns()) {
            spdlog::get("illixr")->error("[record_logger] {} elements passed, but rh for {} only specifies {}.", values_.size(),
                                         record_header_->get().get_name(), record_header_->get().get_columns());
            abort();
        }
        for (std::size_t column = 0; column < values_.size(); ++column) {
            if (values_[column].type() != record_header_->get().get_column_type(column)) {
                spdlog::get("illixr")->error("[record_logger] Caller got wrong type for column {} of {}.", column,
                                             record_header_->get().get_name());
                spdlog::get("illixr")->error("[record_logger] Caller passed: {}; record_header specifies: {}",
                                             values_[column].type().name(),
                                             record_header_->get().get_column_type(column).name());
                abort();
            }
        }
#endif
    }

    record() = default;

    ~record() {
#ifndef NDEBUG
        if (record_header_ && !data_use_indicator_.is_used()) {
            spdlog::get("illixr")->error("[record_logger] Record was deleted without being logged.");
            abort();
        }
#endif
    }

    template<typename T>
    T get_value(unsigned column) const {
#ifndef NDEBUG
        assert(record_header_);
        data_use_indicator_.mark_used();
        if (record_header_->get().get_column_type(column) != typeid(T)) {
            std::ostringstream ss;
            ss << "Caller column type for " << column << " of " << record_header_->get().get_name() << ". "
               << "Caller passed: " << typeid(T).name() << "; "
               << "record_header specifies: " << record_header_->get().get_column_type(column).name() << ". ";
            throw std::runtime_error{ss.str()};
        }
#endif
        return std::any_cast<T>(values_[column]);
    }

    const record_header& get_record_header() const {
        assert(record_header_);
        return record_header_->get();
    }

    [[maybe_unused]] void mark_used() const {
#ifndef NDEBUG
        assert(record_header_);
        data_use_indicator_.mark_used();
#endif
    }

private:
    // Holding a pointer to a record_header is more efficient than
    // requiring each record to hold a list of its column names
    // and table name_. This is just one pointer.
    std::optional<std::reference_wrapper<const record_header>> record_header_;
    std::vector<std::any>                                      values_;
#ifndef NDEBUG
    data_use_indicator data_use_indicator_;
#endif
};

/**
 * @brief The ILLIXR logging service for structured records.
 *
 * This has two advantages over printf logging. It has lower
 * overhead (because it goes into a database), won't result in
 * spliced messages (no stdout race-conditions), and is used
 * uniformly by ILLIXR components.
 */
class record_logger : public phonebook::service {
public:
    ~record_logger() override = default;

    /**
     * @brief Writes one log record.
     */
    virtual void log(const record& r) = 0;

    /**
     * @brief Writes many of the same type of log record.
     *
     * This is more efficient than calling log many times.
     */
    virtual void log(const std::vector<record>& rs) {
        for (const record& r : rs) {
            log(r);
        }
    }
};

/**
 * @brief This class generates unique IDs
 *
 * If you need unique IDs (e.g. for each component), have each component call this class through
 * Phonebook. It returns unique IDs.
 *
 * You can use namespaces to express logical containment. The return value will be unique
 * _between other `get` calls to the same namespace_. This is useful for components and
 * sub-components. For example, If component with id_ 0 has 3 subcomponents, one might call
 * get(0) to name_ each of them. Then, suppose component with id_ 1 has 2 subcomponents, one might
 * call get(1) twice to name_ those. The subcomponent IDs could be reused (non-unique), but tuple
 * (component id_, subcomponent id_) will still be unique. You can also just use the global
 * namespace for everything, if you do not care about generating small integers for the IDs.
 *
 */
class gen_guid : public phonebook::service {
public:
    /**
     * @brief Generate a number, unique from other calls to the same namespace/sub-namespace/sub-sub-namespace.
     */
    std::size_t get(std::size_t namespace_ = 0, std::size_t sub_namespace = 0, std::size_t sub_sub_namespace = 0) {
        if (guid_starts_[namespace_][sub_namespace].count(sub_sub_namespace) == 0) {
            guid_starts_[namespace_][sub_namespace][sub_sub_namespace].store(1);
        }
        return guid_starts_[namespace_][sub_namespace][sub_sub_namespace]++;
    }

private:
    std::unordered_map<std::size_t, std::unordered_map<std::size_t, std::unordered_map<std::size_t, std::atomic<std::size_t>>>>
        guid_starts_;
};

static std::chrono::milliseconds LOG_BUFFER_DELAY{1000};

/**
 * @brief Coalesces logs of the same type to be written back as a single-transaction.
 *
 * Records should all be of the same type.
 * TODO: remove this constraint. Use `log<record_type>(Args... args)` and `std::forward`.
 *
 * In some backend-implementations, logging many logs of the same type is more efficient than
 * logging them individually; However, the client often wants to produce one log-record at a
 * time. This class resolves this mismatch by buffering logs from the client. Every time a new
 * log is added, an internal decision process determines whether or not to flush the buffer, or
 * keep accumulating and wait until later.
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
 * lc.log(make_my_record(id_, it, skip_it, ...));
 * \endcode
 *
 */
class record_coalescer {
public:
    explicit record_coalescer(std::shared_ptr<record_logger> logger_)
        : logger_{std::move(logger_)}
        , last_log_{std::chrono::high_resolution_clock::now()} { }

    ~record_coalescer() {
        flush();
    }

    /**
     * @brief Appends a log to the buffer, which will eventually be written.
     */
    void log(const record& r) {
        if (logger_) {
            buffer_.push_back(r);
            // Log coalescer should only be used with
            // In the common case, they will be the same pointer, quickly check the pointers.
            // In the less common case, we check for object-structural equality.
#ifndef NDEBUG
            if (&r.get_record_header() != &buffer_[0].get_record_header() &&
                r.get_record_header() == buffer_[0].get_record_header()) {
                spdlog::get("illixr")->error("[record_logger] Tried to push a record of type {} to a record logger for type {}",
                                             r.get_record_header().to_string(), buffer_[0].get_record_header().to_string());
                abort();
            }
#endif
            maybe_flush();
        }
    }

    /**
     * @brief Use internal decision process, and possibly trigger flush.
     */
    void maybe_flush() {
        if (std::chrono::high_resolution_clock::now() > last_log_ + LOG_BUFFER_DELAY) {
            flush();
        }
    }

    /**
     * @brief Flush buffer of logs to the underlying logger.
     */
    void flush() {
        if (logger_) {
            std::vector<record> buffer2;
            buffer_.swap(buffer2);
            logger_->log(buffer2);
            last_log_ = std::chrono::high_resolution_clock::now();
        }
    }

    explicit operator bool() const {
        return bool(logger_);
    }

private:
    std::shared_ptr<record_logger>                              logger_;
    std::chrono::time_point<std::chrono::high_resolution_clock> last_log_;
    std::vector<record>                                         buffer_;
};
} // namespace ILLIXR
