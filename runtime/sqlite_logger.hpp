#include <memory>
#include <iostream>
#include <cassert>
#include <functional>
#include <chrono>
#include <atomic>
#include <thread>
#include <experimental/filesystem>
#include "blockingconcurrentqueue.hpp"
#include "sqlite3pp.hpp"
#include "common/logging.hpp"

/**
 * There are many SQLite3 wrapper libraries.
 * [List source](http://srombauts.github.io/SQLiteCpp/#see-also---some-other-simple-c-sqlite-wrappers)
 * TODO: this
 */

namespace ILLIXR {

class sqlite_thread {
public:
	sqlite3pp::database prep_db() {
		if (!std::experimental::filesystem::exists(dir)) {
			std::experimental::filesystem::create_directory(dir);
		}

		std::string path = dir / (table_name + std::string{".sqlite"});
		return sqlite3pp::database{path.c_str()};
	}

	sqlite3pp::command prep_cmd() {
		std::string drop_table_string = std::string{"DROP TABLE IF EXISTS "} + table_name + std::string{";"};
		db.execute(drop_table_string.c_str());

		std::string create_table_string = std::string{"CREATE TABLE "} + table_name + std::string{"("};
		for (unsigned i = 0; i < rh.get_columns(); ++i) {
			create_table_string += rh.get_column_name(i) + std::string{" "};
			if (false) {
			} else if (rh.get_column_type(i) == typeid(std::size_t)) {
				create_table_string += std::string{"INTEGER"};
			} else if (rh.get_column_type(i) == typeid(std::chrono::nanoseconds)) {
				create_table_string += std::string{"INTEGER"};
			} else if (rh.get_column_type(i) == typeid(std::chrono::high_resolution_clock::time_point)) {
				create_table_string += std::string{"INTEGER"};
			} else if (rh.get_column_type(i) == typeid(std::string)) {
				create_table_string += std::string{"TEXT"};
			} else {
				throw std::runtime_error{std::string{"type "} + std::string{rh.get_column_type(i).name()} + std::string{" not found"}};
			}
			create_table_string += std::string{", "};
		}
		create_table_string.erase(create_table_string.size() - 2);
		create_table_string += std::string{");"};
		db.execute(create_table_string.c_str());

		std::string insert_string = std::string{"INSERT INTO "} + table_name + std::string{" VALUES ("};
		for (unsigned i = 0; i < rh.get_columns(); ++i) {
			insert_string += std::string{"?"} + std::to_string(i+1) + std::string{", "};
		}
		insert_string.erase(insert_string.size() - 2);
		insert_string += std::string{");"};
		return sqlite3pp::command{db, insert_string.c_str()};
	}

	sqlite_thread(const record_header& rh_)
		: rh{rh_}
		, table_name{rh.get_name()}
		, db{prep_db()}
		, cmd{prep_cmd()}
		, thread{std::bind(&sqlite_thread::pull_queue, this)}
	{ }

	void pull_queue() {
		record r;
		while (!terminate.load()) {
			// TODO(performance): use timed bulk dequeue and SQL transactions
			if (queue.try_dequeue(r)) {
				process(r);
			}
		}
		while (queue.try_dequeue(r)) {
			process(r);
		}
	}

	void process(const record& r) {
		std::string insert_string = std::string{"INSERT INTO "} + table_name + std::string{" VALUES ("};
		for (unsigned i = 0; i < rh.get_columns(); ++i) {
			insert_string += std::string{"?"} + std::to_string(i+1) + std::string{", "};
		}
		insert_string.erase(insert_string.size() - 2);
		insert_string += std::string{");"};
		sqlite3pp::command cmd{db, insert_string.c_str()};

		for (unsigned i = 0; i < rh.get_columns(); ++i) {
			/*
			  If you get a `std::bad_any_cast` here, make sure the user didn't lie about record.get_record_header().
			  The types there should be the same as those in record.get_values().
			*/
			if (false) {
			} else if (rh.get_column_type(i) == typeid(std::size_t)) {
				cmd.bind(i+1, static_cast<long long>(r.get_value<std::size_t>(i)));
			} else if (rh.get_column_type(i) == typeid(std::chrono::nanoseconds)) {
				cmd.bind(i+1, static_cast<long long>(r.get_value<std::chrono::nanoseconds>(i).count()));
			} else if (rh.get_column_type(i) == typeid(std::chrono::high_resolution_clock::time_point)) {
				auto val = r.get_value<std::chrono::high_resolution_clock::time_point>(i).time_since_epoch();
				cmd.bind(i+1, static_cast<long long>(std::chrono::duration_cast<std::chrono::nanoseconds>(val).count()));
			} else if (rh.get_column_type(i) == typeid(std::string)) {
				// r.get_value<std::string>(i) returns a std::string temporary
				// c_str() returns a pointer into that std::string temporary
				// Therefore, need to copy.
				cmd.bind(i+1, r.get_value<std::string>(i).c_str(), sqlite3pp::copy);
			} else {
				throw std::runtime_error{std::string{"type "} + std::string{rh.get_column_type(i).name()} + std::string{" not implemented"}};
			}
		}
		cmd.execute();
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
	sqlite3pp::command cmd;
	moodycamel::BlockingConcurrentQueue<record> queue;
	std::atomic<bool> terminate {false};
	std::thread thread;
};

const std::experimental::filesystem::path sqlite_thread::dir {"metrics"};

class sqlite_metric_logger : public c_metric_logger {
private:
	sqlite_thread& get_sqlite_thread(const record& r) {
		const record_header& rh = r.get_record_header();
		auto result = registered_tables.find(rh.get_id());
		if (result != registered_tables.cend()) {
			return result->second;
		} else {
			const std::lock_guard lock{_m_registry_lock};
			auto pair = registered_tables.try_emplace(rh.get_id(), rh);
			return pair.first->second;
		}
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
	std::mutex _m_registry_lock;
};

}
