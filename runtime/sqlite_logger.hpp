#include <memory>
#include <iostream>
#include <cassert>
#include <functional>
#include <chrono>
#include <atomic>
#include <thread>
#include <filesystem>
#include "blockingconcurrentqueue.hpp"
#include "sqlite3pp.hpp"
#include "common/logging.hpp"
#include "common/record_types.hpp"

/**
 * There are many SQLite3 wrapper libraries.
 * [List source](http://srombauts.github.io/SQLiteCpp/#see-also---some-other-simple-c-sqlite-wrappers)
 * TODO: this
 */

namespace ILLIXR {

class sqlite_thread {
public:
	sqlite3pp::database prep_db() {
		if (!std::filesystem::exists(dir)) {
			std::filesystem::create_directory(dir);
		}

		std::string path = dir / (table_name + std::string{".sqlite"});
		return sqlite3pp::database{path.c_str()};
	}

	sqlite3pp::command prep_cmd() {
		std::string drop_table_string = std::string{"DROP TABLE IF EXISTS "} + table_name + std::string{";"};
		db.execute(drop_table_string.c_str());

		std::string create_table_string = std::string{"CREATE TABLE "} + table_name + std::string{"("};
		for (const std::pair<std::string, const type*>& ty : record_type.fields) {
			create_table_string += ty.first + std::string{" "};
			if (false) {
			} else if (ty.second->type_id == types::std__chrono__nanoseconds.type_id) {
				create_table_string += std::string{"INTEGER"};
			} else if (ty.second->type_id == types::std__size_t.type_id) {
				create_table_string += std::string{"INTEGER"};
			} else if (ty.second->type_id == types::std__string.type_id) {
				create_table_string += std::string{"TEXT"};
			} else {
				throw std::runtime_error{std::string{"type_id "} + std::to_string(ty.second->type_id) + std::string{" not found"}};
			}
			create_table_string += std::string{","};
		}
		create_table_string.erase(create_table_string.size() - 1);
		create_table_string += std::string{");"};
		db.execute(create_table_string.c_str());

		std::string insert_string = std::string{"INSERT INTO "} + table_name + std::string{" VALUES ("};
		std::size_t i = 1;
		for ([[maybe_unused]] const std::pair<std::string, const type*>& ty : record_type.fields) {
			insert_string += std::string{"?"} + std::to_string(i) + std::string{","};
			++i;
		}
		insert_string.erase(insert_string.size() - 1);
		insert_string += std::string{");"};
		return sqlite3pp::command{db, insert_string.c_str()};
	}

	sqlite_thread(const struct_type& record_type_)
		: record_type{record_type_}
		, table_name{record_type.name + std::to_string(record_type.type_id)}
		, db{prep_db()}
		, cmd{prep_cmd()}
		, thread{std::bind(&sqlite_thread::pull_queue, this)}
	{ }

	void pull_queue() {
		while (!terminate.load()) {
			std::size_t chunk_size = 1024;
			std::vector<std::unique_ptr<const record>> buffer {chunk_size};
			std::size_t dequeued_count = queue.wait_dequeue_bulk_timed(buffer.begin(), chunk_size, std::chrono::milliseconds(50));
			if (dequeued_count != 0) {
				// sqlite3pp::transaction xct {db};
				{
					for (std::size_t i = 0; i < dequeued_count; ++i) {

						std::string insert_string = std::string{"INSERT INTO "} + table_name + std::string{" VALUES ("};
						{
							std::size_t i = 1;
							for ([[maybe_unused]] const std::pair<std::string, const type*>& ty : record_type.fields) {
								insert_string += std::string{"?"} + std::to_string(i) + std::string{","};
								++i;
							}
						}
						insert_string.erase(insert_string.size() - 1);
						insert_string += std::string{");"};
						sqlite3pp::command cmd{db, insert_string.c_str()};


						std::size_t j = 1;
						const char* r = reinterpret_cast<const char*>(buffer[i].get());
						for (const std::pair<std::string, const type*>& ty : record_type.fields) {
							if (false) {
							} else if (ty.second->type_id == types::std__chrono__nanoseconds.type_id) {
								std::chrono::nanoseconds data = *reinterpret_cast<const std::chrono::nanoseconds*>(r);
								cmd.bind(j, static_cast<long long int>(data.count()));
							} else if (ty.second->type_id == types::std__size_t.type_id) {
								long long int data = *reinterpret_cast<const std::size_t*>(r);
								cmd.bind(j, data);
							} else if (ty.second->type_id == types::std__string.type_id) {
								std::string data = *reinterpret_cast<const std::string*>(r);
								cmd.bind(j, data.c_str(), sqlite3pp::copy);
							}
							j++;
							r += ty.second->size;
						}
						cmd.execute();
					}
				}
				// xct.commit();
			}
		}
	}

	void put_queue(std::vector<std::unique_ptr<const record>>&& buffer_in) {
		queue.enqueue_bulk(std::make_move_iterator(buffer_in.begin()), buffer_in.size());
	}

	void put_queue(std::unique_ptr<const record>&& record_in) {
		queue.enqueue(std::move(record_in));
	}

	~sqlite_thread() {
		terminate.store(true);
		thread.join();
	}

private:
	static const std::filesystem::path dir;
	const struct_type& record_type;
	std::string table_name;
	sqlite3pp::database db;
	sqlite3pp::command cmd;
	std::thread thread;
	moodycamel::BlockingConcurrentQueue<std::unique_ptr<const record>> queue;
	std::atomic<bool> terminate {false};
};

const std::filesystem::path sqlite_thread::dir {"metrics"};

class sqlite_metric_logger : public c_metric_logger {
protected:
	virtual void log_many2(const struct_type* ty, std::vector<std::unique_ptr<const record>>&& r) override {
		assert(ty);
		if (registered_tables.count(ty->type_id) == 0) {
			const std::lock_guard lock{_m_registry_lock};
			if (registered_tables.count(ty->type_id) == 0) {
				registered_tables.try_emplace(ty->type_id, *ty);
			}
		}
		registered_tables.at(ty->type_id).put_queue(std::move(r));
	}

	virtual void log2(const struct_type* ty, std::unique_ptr<const record>&& r) override {
		assert(ty);

		// Speculatively check if table is already registered
		if (registered_tables.count(ty->type_id) == 0) {
			const std::lock_guard lock{_m_registry_lock};
			// We still have to double-check if table is already registered
			// To be sure, because the previous check did not hold a lock.
			if (registered_tables.count(ty->type_id) == 0) {
				registered_tables.try_emplace(ty->type_id, *ty);
			}
		}
		registered_tables.at(ty->type_id).put_queue(std::move(r));
	}

private:
	std::unordered_map<std::size_t, sqlite_thread> registered_tables;
	std::mutex _m_registry_lock;
};

}
