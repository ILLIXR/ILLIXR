#pragma once

#include "illixr/concurrentqueue/blockingconcurrentqueue.hpp"
#include "illixr/error_util.hpp"
#include "illixr/global_module_defs.hpp"
#include "illixr/record_logger.hpp"
#include "sqlite3pp/sqlite3pp.hpp"

#include <filesystem>
#include <iostream>
#include <mutex>
#include <shared_mutex>
#include <spdlog/spdlog.h>
#include <thread>

/**
 * There are many SQLite3 wrapper libraries.
 * [List source](http://srombauts.github.io/SQLiteCpp/#see-also---some-other-simple-c-sqlite-wrappers)
 * TODO: this
 */

namespace ILLIXR {

class sqlite_thread {
public:
    sqlite3pp::database prep_db() {
        RAC_ERRNO_MSG("sqlite_record_logger at start of prep_db");

        if (!std::filesystem::exists(directory_)) {
            std::filesystem::create_directory(directory_);
        }

        const std::string path = directory_.string() + "/" + (table_name_ + std::string{".sqlite"});

        RAC_ERRNO_MSG("sqlite_record_logger before sqlite3pp::database");
        sqlite3pp::database database{path.c_str()};
        RAC_ERRNO_MSG("sqlite_record_logger after sqlite3pp::database");

        return database;
    }

    std::string prep_insert_str() {
        RAC_ERRNO_MSG("sqlite_record_logger at start of prep_insert_str");

        std::string drop_table_string = std::string{"DROP TABLE IF EXISTS "} + table_name_ + std::string{";"};
        database_.execute(drop_table_string.c_str());
        RAC_ERRNO_MSG("sqlite_record_logger after drop table execute.");

        std::string create_table_string = std::string{"CREATE TABLE "} + table_name_ + std::string{"("};
        for (unsigned int i = 0; i < record_header_.get_columns(); ++i) {
            create_table_string += record_header_.get_column_name(i) + std::string{" "};
            const std::type_info& rh_type = record_header_.get_column_type(i);
            if (rh_type == typeid(std::size_t) || rh_type == typeid(bool) || rh_type == typeid(std::chrono::nanoseconds) ||
                rh_type == typeid(std::chrono::high_resolution_clock::time_point) || rh_type == typeid(duration) ||
                rh_type == typeid(time_point)) {
                create_table_string += std::string{"INTEGER"};
            } else if (rh_type == typeid(double)) {
                create_table_string += std::string{"REAL"}; // For performance timing. Will be deleted when implementing #208.
            } else if (rh_type == typeid(std::string)) {
                create_table_string += std::string{"TEXT"};
            } else {
                throw std::runtime_error{std::string{"type "} + std::string{record_header_.get_column_type(i).name()} +
                                         std::string{" not found"}};
            }
            create_table_string += std::string{", "};
        }
        create_table_string.erase(create_table_string.size() - 2);
        create_table_string += std::string{");"};

        RAC_ERRNO_MSG("sqlite_record_logger before create table execute");
        database_.execute(create_table_string.c_str());
        RAC_ERRNO_MSG("sqlite_record_logger after create table execute");

        std::string insert_string = std::string{"INSERT INTO "} + table_name_ + std::string{" VALUES ("};
        for (unsigned int i = 0; i < record_header_.get_columns(); ++i) {
            insert_string += std::string{"?"} + std::to_string(i + 1) + std::string{", "};
        }
        insert_string.erase(insert_string.size() - 2);
        insert_string += std::string{");"};
        return insert_string;
    }

    explicit sqlite_thread(const record_header& rh_)
        : record_header_{rh_}
        , table_name_{record_header_.get_name()}
        , database_{prep_db()}
        , insert_str_{prep_insert_str()}
        , insert_cmd_{database_, insert_str_.c_str()}
        , thread_{[this] {
            pull_queue();
        }} { }

    void pull_queue() {
        const std::size_t   max_record_batch_size = 1024 * 256;
        std::vector<record> record_batch{max_record_batch_size};
        std::size_t         actual_batch_size;

        spdlog::get("illixr")->set_pattern("[%Y-%m-%d %H:%M:%S.%e] [%n] [%^%l%$] [sqlite_record_logger] thread_ %t %v");
        spdlog::get("illixr")->debug("{}", table_name_);
        spdlog::get("illixr")->set_pattern("%+");

        std::size_t processed = 0;
        while (!terminate_.load()) {
            std::this_thread::sleep_for(std::chrono::seconds{1});
            // Uncomment this block to log in "real time";
            // Otherwise, everything gets logged "post real time".
            /*
            const std::chrono::seconds max_record_match_wait_time {10};
            actual_batch_size = queue_.wait_dequeue_bulk_timed(record_batch.begin(), record_batch.size(),
            max_record_match_wait_time); process(record_batch, actual_batch_size); processed += actual_batch_size;
            */
        }

        // We got the terminate_ command,
        // So drain whatever is left in the queue_.
        // But don't wait around once it is empty.
        std::size_t post_processed = 0;
        while ((actual_batch_size = queue_.try_dequeue_bulk(record_batch.begin(), record_batch.size()))) {
            process(record_batch, actual_batch_size);
            post_processed += actual_batch_size;
        }
        spdlog::get("illixr")->debug("[sqlite_record_logger] Drained {} (sqlite); {}/{} done post real time", table_name_,
                                     post_processed, (processed + post_processed));
    }

    void process(const std::vector<record>& record_batch, std::size_t batch_size) {
        sqlite3pp::transaction xct{database_};
        for (std::size_t i = 0; i < batch_size; ++i) {
            // TODO(performance): reuse the sqlite3pp statement
            // This currently has to be 'reinterpreted' for every iteration.
            sqlite3pp::command cmd{database_, insert_str_.c_str()};
            const record&      r = record_batch[i];
            for (int j = 0; j < static_cast<int>(record_header_.get_columns()); ++j) {
                /*
                  If you get a `std::bad_any_cast` here, make sure the user didn't lie about record.get_record_header().
                  The types there should be the same as those in record.get_values().
                */
                const std::type_info& rh_type = record_header_.get_column_type(j);
                if (rh_type == typeid(std::size_t)) {
                    cmd.bind(j + 1, static_cast<long long>(r.get_value<std::size_t>(j)));
                } else if (rh_type == typeid(bool)) {
                    cmd.bind(j + 1, static_cast<long long>(r.get_value<bool>(j)));
                } else if (rh_type == typeid(double)) {
                    cmd.bind(j + 1, r.get_value<double>(j));
                } else if (rh_type == typeid(std::chrono::nanoseconds)) {
                    auto val = r.get_value<duration>(j);
                    cmd.bind(j + 1, static_cast<long long>(std::chrono::nanoseconds{val}.count()));
                } else if (rh_type == typeid(std::chrono::high_resolution_clock::time_point)) {
                    auto val = r.get_value<std::chrono::high_resolution_clock::time_point>(j).time_since_epoch();
                    cmd.bind(j + 1, static_cast<long long>(std::chrono::nanoseconds{val}.count()));
                } else if (rh_type == typeid(duration)) {
                    auto val = r.get_value<duration>(j);
                    cmd.bind(j + 1, static_cast<long long>(std::chrono::nanoseconds{val}.count()));
                } else if (rh_type == typeid(time_point)) {
                    auto val = r.get_value<time_point>(j).time_since_epoch();
                    cmd.bind(j + 1, static_cast<long long>(std::chrono::nanoseconds{val}.count()));
                } else if (rh_type == typeid(std::string)) {
                    // r.get_value<std::string>(j) returns a std::string temporary
                    // c_str() returns a pointer into that std::string temporary
                    // Therefore, need to copy.
                    cmd.bind(j + 1, r.get_value<std::string>(j).c_str(), sqlite3pp::copy);
                } else {
                    throw std::runtime_error{std::string{"type "} + std::string{record_header_.get_column_type(j).name()} +
                                             std::string{" not implemented"}};
                }
            }
            RAC_ERRNO_MSG("sqlite_record_logger set errno before process cmd execute");

            cmd.execute();
            RAC_ERRNO_MSG("sqlite_record_logger after process cmd execute");
        }
        xct.commit();
    }

    void put_queue(const std::vector<record>& buffer_in) {
        queue_.enqueue_bulk(buffer_in.begin(), buffer_in.size());
    }

    void put_queue(const record& record_in) {
        queue_.enqueue(record_in);
    }

    ~sqlite_thread() {
        terminate_.store(true);
        thread_.join();
    }

private:
    static const std::filesystem::path          directory_;
    const record_header&                        record_header_;
    std::string                                 table_name_;
    sqlite3pp::database                         database_;
    std::string                                 insert_str_;
    sqlite3pp::command                          insert_cmd_;
    moodycamel::BlockingConcurrentQueue<record> queue_;
    std::atomic<bool>                           terminate_{false};
    std::thread                                 thread_;
};

const std::filesystem::path sqlite_thread::directory_{"metrics"};

class sqlite_record_logger : public record_logger {
protected:
    void log(const std::vector<record>& r) override {
        if (!r.empty()) {
            get_sqlite_thread(r[0]).put_queue(r);
        }
    }

    void log(const record& r) override {
        get_sqlite_thread(r).put_queue(r);
    }

private:
    sqlite_thread& get_sqlite_thread(const record& r) {
        const record_header& record_header_ = r.get_record_header();
        {
            const std::shared_lock<std::shared_mutex> lock{registry_lock_};
            auto                                      result = registered_tables_.find(record_header_.get_id());
            if (result != registered_tables_.cend()) {
                return result->second;
            }
        }
        const std::unique_lock<std::shared_mutex> lock{registry_lock_};
        auto pair = registered_tables_.try_emplace(record_header_.get_id(), record_header_);
        return pair.first->second;
    }

    std::unordered_map<std::size_t, sqlite_thread> registered_tables_;
    std::shared_mutex                              registry_lock_;
};

} // namespace ILLIXR
