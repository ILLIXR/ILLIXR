#include <memory>
#include <iostream>
#include <cassert>
#include <functional>
#include <chrono>
#include <atomic>
#include <thread>
#include <shared_mutex>
#include <experimental/filesystem>
#include <cerrno>
#include "concurrentqueue/blockingconcurrentqueue.hpp"
#include "sqlite3pp/sqlite3pp.hpp"
#include "common/record_logger.hpp"
#include "common/global_module_defs.hpp"
#include "common/error_util.hpp"

/**
 * There are many SQLite3 wrapper libraries.
 * [List source](http://srombauts.github.io/SQLiteCpp/#see-also---some-other-simple-c-sqlite-wrappers)
 * TODO: this
 */

namespace ILLIXR {

class sqlite_thread {
public:
	sqlite3pp::database prep_db() {
        assert(errno == 0 && "Errno should not be set at start of prep_db");

		if (!std::experimental::filesystem::exists(dir)) {
			std::experimental::filesystem::create_directory(dir);
		}

		const std::string path = dir / (table_name + std::string{".sqlite"});

		RAC_ERRNO_MSG("sqlite_record_logger before sqlite3pp::database");
        sqlite3pp::database db{path.c_str()};
        RAC_ERRNO_MSG("sqlite_record_logger after sqlite3pp::database");

		return db;
	}

	std::string prep_insert_str() {
		assert(errno == 0 && "Errno should not be set at start of prep_insert_str");

		std::string drop_table_string = std::string{"DROP TABLE IF EXISTS "} + table_name + std::string{";"};
		db.execute(drop_table_string.c_str());
		RAC_ERRNO_MSG("sqlite_record_logger after drop table execute.");

		std::string create_table_string = std::string{"CREATE TABLE "} + table_name + std::string{"("};
		for (unsigned int i = 0; i < rh.get_columns(); ++i) {
			create_table_string += rh.get_column_name(i) + std::string{" "};
			if (false) {
			} else if (rh.get_column_type(i) == typeid(std::size_t)) {
				create_table_string += std::string{"INTEGER"};
			} else if (rh.get_column_type(i) == typeid(bool)) {
				create_table_string += std::string{"INTEGER"};
			} else if (rh.get_column_type(i) == typeid(std::chrono::nanoseconds)) {
				create_table_string += std::string{"INTEGER"};
			} else if (rh.get_column_type(i) == typeid(std::chrono::high_resolution_clock::time_point)) {
				create_table_string += std::string{"INTEGER"};
			} else if (rh.get_column_type(i) == typeid(std::string)) {
				create_table_string += std::string{"TEXT"};
			} else if (rh.get_column_type(i) == typeid(double)) {
				create_table_string += std::string{"REAL"};
			} else {
				throw std::runtime_error{std::string{"type "} + std::string{rh.get_column_type(i).name()} + std::string{" not found"}};
			}
			create_table_string += std::string{", "};
		}
		create_table_string.erase(create_table_string.size() - 2);
		create_table_string += std::string{");"};

		assert(errno == 0 && "Errno should not be set before create table execute");
		db.execute(create_table_string.c_str());
		RAC_ERRNO_MSG("sqlite_record_logger after create table execute");

		std::string insert_string = std::string{"INSERT INTO "} + table_name + std::string{" VALUES ("};
		for (unsigned int i = 0; i < rh.get_columns(); ++i) {
			insert_string += std::string{"?"} + std::to_string(i+1) + std::string{", "};
		}
		insert_string.erase(insert_string.size() - 2);
		insert_string += std::string{");"};
		return insert_string;
	}

	sqlite_thread(const record_header& rh_)
		: rh{rh_}
		, table_name{rh.get_name()}
		, db{prep_db()}
		, insert_str{prep_insert_str()}
		, insert_cmd{db, insert_str.c_str()}
		, thread{std::bind(&sqlite_thread::pull_queue, this)}
	{ }

	void pull_queue() {
		const std::size_t max_record_batch_size = 1024 * 256;
		std::vector<record> record_batch {max_record_batch_size};
		std::size_t actual_batch_size;

		std::cout << "thread," << std::this_thread::get_id() << ",sqlite thread," << table_name << std::endl;

		std::size_t processed = 0;
		while (!terminate.load()) {
			std::this_thread::sleep_for(std::chrono::seconds{1});
			// Uncomment this block to log in "real time";
			// Otherwise, everything gets loged "post real time".
			/*
			const std::chrono::seconds max_record_match_wait_time {10};
			actual_batch_size = queue.wait_dequeue_bulk_timed(record_batch.begin(), record_batch.size(), max_record_match_wait_time);
			process(record_batch, actual_batch_size);
			processed += actual_batch_size;
			*/
		}

		// We got the terminate commnad,
		// So drain whatever is left in the queue.
		// But don't wait around once it is empty.
		std::size_t post_processed = 0;
		while ((actual_batch_size = queue.try_dequeue_bulk(record_batch.begin(), record_batch.size()))) {
			process(record_batch, actual_batch_size);
			post_processed += actual_batch_size;
		}
		std::cerr << "Drained " << table_name << " (sqlite); " << post_processed << " / " << (processed + post_processed) << " done post real time" << std::endl;
	}

	void process(const std::vector<record>& record_batch, std::size_t batch_size) {
		sqlite3pp::transaction xct{db};
		for (std::size_t i = 0; i < batch_size; ++i) {
			// TODO(performance): reuse the sqlite3pp statement
			// This currently has to be 'reinterpreted' for every iteration.
			sqlite3pp::command cmd{db, insert_str.c_str()};
			const record& r = record_batch[i];
			for (unsigned int j = 0; j < rh.get_columns(); ++j) {
				/*
				  If you get a `std::bad_any_cast` here, make sure the user didn't lie about record.get_record_header().
				  The types there should be the same as those in record.get_values().
				*/
				if (false) {
				} else if (rh.get_column_type(j) == typeid(std::size_t)) {
					cmd.bind(j+1, static_cast<long long>(r.get_value<std::size_t>(j)));
				} else if (rh.get_column_type(j) == typeid(bool)) {
					cmd.bind(j+1, static_cast<long long>(r.get_value<bool>(j)));
				} else if (rh.get_column_type(j) == typeid(double)) {
					cmd.bind(j+1, r.get_value<double>(j));
				} else if (rh.get_column_type(j) == typeid(std::chrono::nanoseconds)) {
					cmd.bind(j+1, static_cast<long long>(r.get_value<std::chrono::nanoseconds>(j).count()));
				} else if (rh.get_column_type(j) == typeid(std::chrono::high_resolution_clock::time_point)) {
					auto val = r.get_value<std::chrono::high_resolution_clock::time_point>(j).time_since_epoch();
					cmd.bind(j+1, static_cast<long long>(std::chrono::duration_cast<std::chrono::nanoseconds>(val).count()));
				} else if (rh.get_column_type(j) == typeid(std::string)) {
					// r.get_value<std::string>(j) returns a std::string temporary
					// c_str() returns a pointer into that std::string temporary
					// Therefore, need to copy.
					cmd.bind(j+1, r.get_value<std::string>(j).c_str(), sqlite3pp::copy);
				} else {
					throw std::runtime_error{std::string{"type "} + std::string{rh.get_column_type(j).name()} + std::string{" not implemented"}};
				}
			}
			assert(errno == 0 && "Errno should not be set before process cmd execute");
			cmd.execute();
			RAC_ERRNO_MSG("sqlite_record_logger after process cmd execute");
		}
		xct.commit();
	}

	void put_queue(const std::vector<record>& buffer_in) {
		queue.enqueue_bulk(buffer_in.begin(), buffer_in.size());
	}

	void put_queue(const record& record_in) {
		queue.enqueue(record_in);
	}

	~sqlite_thread() {
		terminate.store(true);
		thread.join();
	}

private:
	static const std::experimental::filesystem::path dir;
	const record_header& rh;
	std::string table_name;
	sqlite3pp::database db;
	std::string insert_str;
	sqlite3pp::command insert_cmd;
	moodycamel::BlockingConcurrentQueue<record> queue;
	std::atomic<bool> terminate {false};
	std::thread thread;
};

const std::experimental::filesystem::path sqlite_thread::dir {"metrics"};

class sqlite_record_logger : public record_logger {
private:
	sqlite_thread& get_sqlite_thread(const record& r) {
		const record_header& rh = r.get_record_header();
		{
			const std::shared_lock<std::shared_mutex> lock {_m_registry_lock};
			auto result = registered_tables.find(rh.get_id());
			if (result != registered_tables.cend()) {
				return result->second;
			}
		}
		const std::unique_lock<std::shared_mutex> lock{_m_registry_lock};
		auto pair = registered_tables.try_emplace(rh.get_id(), rh);
		return pair.first->second;
	}

protected:
	virtual void log(const std::vector<record>& r) override {
		if (!r.empty()) {
			get_sqlite_thread(r[0]).put_queue(r);
		}
	}

	virtual void log(const record& r) override {
		get_sqlite_thread(r).put_queue(r);
	}

private:
	std::unordered_map<std::size_t, sqlite_thread> registered_tables;
	std::shared_mutex _m_registry_lock;
};

}
