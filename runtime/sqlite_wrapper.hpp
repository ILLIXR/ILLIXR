#pragma once
#include <ostream>
#include <string>
#include <string_view>
#include <sstream>
#include <variant>
#include <vector>

#include <boost/filesystem.hpp>
#include <sqlite3.h>

namespace ILLIXR {

	namespace sqlite {

		class statement_builder;
		class statement;

		static void sqlite_error_to_exception(int rc, const char* activity, std::string info = std::string{}) {
			if (rc != SQLITE_OK && rc != SQLITE_ROW && rc != SQLITE_DONE) {
				std::cerr << activity << ": " << sqlite3_errstr(rc) << ": " << info << std::endl;
				assert(0);
			}
		}

		class type {
		private:
			const std::string_view name;
			std::string cpp_name;
		public:
			type(const std::string_view name_, std::string&& cpp_name_)
				: name{name_}
				, cpp_name{cpp_name_}
			{ }

			type(const char* name_)
				: type{name_, name_}
			{ }

			const std::string_view get_name() const { return name; }
			const std::string& get_cpp_name() const { return cpp_name; }

			bool operator==(const type& other) const {
				return this == &other;
			}
		};
		static const type type_NULL {"NULL", "NULL"};
		static const type type_INTEGER {"INTEGER"};
		static const type type_REAL {"REAL"};
		static const type type_TEXT {"TEXT"};
		static const type type_BLOB {"BLOB"};

		static std::ostream& operator<<(std::ostream& os, const type& type_) {
			return os << "type_" << type_.get_cpp_name();
		}

		class field {
		private:
			std::string _m_name;
			const type* _m_type;

		public:
			field(std::string&& name, const type& type)
				: _m_name{name}
				, _m_type{&type}
			{ }

			const std::string& get_name() const { return _m_name; }
			const type& get_type() const { return *_m_type; }
		};

		static std::ostream& operator<<(std::ostream& os, const field& field_) {
			return os << "field{" << field_.get_name() << ", " << field_.get_type() << "}";
		}


		class schema {
		private:
			std::vector<field> _m_fields;

		public:
			schema(std::vector<field>&& fields)
				: _m_fields{std::move(fields)}
			{ }

			schema operator+(const schema& other) const {
				std::vector<field> resulting_fields {_m_fields.cbegin(), _m_fields.cend()};
				resulting_fields.insert(resulting_fields.end(), other._m_fields.cbegin(), other._m_fields.cend());
				return schema{std::move(resulting_fields)};
			}

			const std::vector<field>& get_fields() const { return _m_fields; }
		};

		static std::ostream& operator<<(std::ostream& os, const schema& schema_) {
			os << "schema{{";
			bool first = true;
			for (const field& field : schema_.get_fields()) {
				if (!first) {
					os << ", ";
				}
				os << field;
				first = false;
			}
			return os << "}}";
		}

		class value {
		public:
			using value_variant = std::variant<uint64_t, double, std::string_view, std::vector<char>, std::nullptr_t, bool>;
		private:
			std::reference_wrapper<const type> _m_type;
			value_variant _m_data;
			size_t _m_id;
			static constexpr const size_t no_id = 1 << 31;
		public:
			value(value_variant&& data, size_t id = no_id)
				: _m_type{
					std::holds_alternative<uint64_t>(data) ? std::cref(type_INTEGER) :
					std::holds_alternative<double>(data) ? std::cref(type_REAL) :
					std::holds_alternative<std::string_view>(data) ? std::cref(type_TEXT) :
					std::holds_alternative<std::vector<char>>(data) ? std::cref(type_BLOB) :
					std::holds_alternative<std::nullptr_t>(data) ? std::cref(type_NULL) :
					({assert(0 && "Unknown type"); std::cref(type_NULL);})
				}
				, _m_data{false}
				, _m_id{id}
			{
				set_data(std::move(data));
			}
			value(const type& type_, value_variant&& data, size_t id = no_id)
				: _m_type{std::cref(type_)}
				, _m_data{false}
				, _m_id{id}
			{
				if (!std::holds_alternative<bool>(data) || std::get<bool>(data)) {
					set_data(std::move(data));
				}
			}

			static value placeholder(const type& type_, size_t id = no_id) {
				return value{type_, false, id};
			}

			const type& get_type() const { return _m_type; }

			void set_id(size_t id) { assert(!has_id()); _m_id = id; }
			bool has_id() const { return _m_id != no_id; }
			size_t get_id() const { assert(has_id()); return _m_id; }

			void set_data(value_variant&& data) {
				assert(!slot_filled());
				if (false) {
				} else if (_m_type.get() == type_INTEGER) {
					assert(std::holds_alternative<uint64_t>(data));
				} else if (_m_type.get() == type_REAL) {
					assert(std::holds_alternative<double>(data));
				} else if (_m_type.get() == type_TEXT) {
					assert(std::holds_alternative<std::string_view>(data));
				} else if (_m_type.get() == type_BLOB) {
					assert(std::holds_alternative<std::vector<char>>(data));
				} else if (_m_type.get() == type_NULL) {
					assert(std::holds_alternative<std::nullptr_t>(data));
				} else {
					assert(0 && "Unkown type");
				}
				_m_data = std::move(data);
			}
			const value_variant& get_data() const {
				assert(has_data());
				return _m_data;
			}
			bool slot_filled() const { return !std::holds_alternative<bool>(_m_data) || std::get<bool>(_m_data); }
			bool has_data() const { return !std::holds_alternative<bool>(_m_data); }
			void mark_slot_filled() {
				assert(!slot_filled());
				_m_data = true;
			}
			void mark_slot_unfilled() {
				assert(slot_filled());
				_m_data = false;
			}
		};

		class database;
		class statement_builder;

		class statement {
		private:
			friend class statement_builder;
			std::vector<value> _m_val_map;
			sqlite3_stmt* _m_stmt;

			statement(database& db, std::string&& cmd, std::vector<value>&& val_map);

			void bind(value var) {
				int rc = 0;
				if (false) {
				} else if (&var.get_type() == &type_INTEGER) {
					auto data = std::get<uint64_t>(var.get_data());
					rc = sqlite3_bind_int64(_m_stmt, var.get_id() + 1, data);
				} else if (&var.get_type() == &type_REAL) {
					auto data = std::get<double>(var.get_data());
					rc = sqlite3_bind_double(_m_stmt, var.get_id() + 1, data);
				} else if (&var.get_type() == &type_TEXT) {
					const auto& data = std::get<std::string_view>(var.get_data());
					rc = sqlite3_bind_text(_m_stmt, var.get_id() + 1, data.data(), data.size(), SQLITE_STATIC);
				} else if (&var.get_type() == &type_BLOB) {
					const auto& data = std::get<std::vector<char>>(var.get_data());
					rc = sqlite3_bind_blob(_m_stmt, var.get_id() + 1, data.data(), data.size(), SQLITE_TRANSIENT);
				} else if (&var.get_type() == &type_NULL) {
					rc = sqlite3_bind_null(_m_stmt, var.get_id() + 1);
				} else {
					assert(0 && "Unkown type");
				}
				sqlite_error_to_exception(rc, "bind");
			}

		public:
			void set(value&& var) {
				size_t id = var.get_id();
				assert(id < _m_val_map.size() && "Variable's integer ID out of range");
				[[maybe_unused]] value& target = _m_val_map.at(id);
				assert(!target.slot_filled() && "Variable's slot is already filled");
				assert(&target.get_type() == &var.get_type() && "Wrong type for variable");
				assert(var.has_data() && "Var must has value to use");
				// TODO(grayson5): avoid this copy
				_m_val_map.at(id) = std::move(var);
				bind(_m_val_map.at(id));
			}

			void step() {
				for ([[maybe_unused]] const auto& pair : _m_val_map) {
					assert(pair.slot_filled());
				}
				int rc = sqlite3_step(_m_stmt);
				sqlite_error_to_exception(rc, "step_stmt");
			}

			void reset() {
				int rc = sqlite3_reset(_m_stmt);
				sqlite_error_to_exception(rc, "reset_stmt");
				for (auto& pair : _m_val_map) {
					pair.mark_slot_unfilled();
				}
			}

			~statement() {
				int rc = sqlite3_finalize(_m_stmt);
				sqlite_error_to_exception(rc, "finalize_stmt");
			}
		};

		class table;

		class database {
		private:
			friend class statement;
			friend class statement_builder;
			std::string _m_url;
			std::unique_ptr<sqlite3, void(*)(sqlite3*)> _m_db;
			bool in_transaction;
			statement begin_transaction_statement;
			statement end_transaction_statement;

			static std::string path_to_string(boost::filesystem::path&& path, bool truncate) {
				if (truncate) {
					if (boost::filesystem::exists(path)) {
						boost::filesystem::remove(path);
					}
				}
				return path.string();
			}

			static std::unique_ptr<sqlite3, void(*)(sqlite3*)> open_db(const char* str) {
				sqlite3* db;
				int rc = sqlite3_open(str, &db);
				sqlite_error_to_exception(rc, "open database");
				return std::unique_ptr<sqlite3, void(*)(sqlite3*)>{db, &close_db};
			}

			static void close_db(sqlite3* db) {
				int rc = sqlite3_close(db);
				sqlite_error_to_exception(rc, "close");
			}

		public:
			database(std::string&& url);

			database(boost::filesystem::path&& path, bool truncate)
				: database{path_to_string(std::move(path), truncate)}
			{ }

			std::string get_url() const { return _m_url; }

			table create_table(std::string&& name, schema&& schema);

			void begin_transaction();
			void end_transaction();
		};

		static std::ostream& operator<<(std::ostream& os, const database& db) {
			return os << "database{\"" << db.get_url() << "\"}";
		}

		enum class sentinel {
			COMMA,
			LEFT_PARENS,
			RIGHT_PARENS
		};

		class keyword {
			const std::string_view sql;
		public:
			keyword(const std::string_view sql_)
				: sql{sql_}
			{ }
			const std::string_view get_sql() const { return sql; }
		};
		static const keyword keyword_CREATE_TABLE {"CREATE TABLE"};
		static const keyword keyword_INSERT_INTO {"INSERT INTO"};
		static const keyword keyword_VALUES {"VALUES"};
		static const keyword keyword_BEGIN_TRANSACTION {"BEGIN TRANSACTION"};
		static const keyword keyword_END_TRANSACTION {"END TRANSACTION"};

		class statement_builder {
		private:
			database& _m_db;
			std::ostringstream _m_cmd_stream;
			size_t _m_first_unused_id;
			std::vector<std::optional<value>> _m_val_map;
			std::deque<bool> _m_comma_waiting;
			size_t _m_id_limit;

			void validate_literal(const std::string_view literal) {
				assert(!sqlite3_keyword_check(literal.data(), literal.size()) && "Can't use a SQLite keyword as a literal");
				bool first = true;
				for (const char letter : literal) {
					[[maybe_unused]] bool valid_id = literal.size() > 0 && (false
						|| (!first && '0' <= letter && letter <= '9')
						|| ('a' <= letter && letter <= 'z')
						|| ('A' <= letter && letter <= 'Z')
						|| letter == '_'
					);
					assert(valid_id && "Must use a [a-zA-Z][a-zA-Z0-9]* as a literal");
					first = false;
				}
			}

			void place_comma() {
				if (!_m_comma_waiting.empty() && _m_comma_waiting.back()) {
					_m_cmd_stream << ", ";
					_m_comma_waiting.back() = false;
				}
			}

		public:
			statement_builder(database& db)
				: _m_db{db}
				, _m_first_unused_id{0}
				, _m_id_limit{static_cast<size_t>(sqlite3_limit(_m_db._m_db.get(), SQLITE_LIMIT_VARIABLE_NUMBER, -1))}
			{ }

			statement_builder& operator<<(const std::string& literal) {
				place_comma();
				validate_literal(literal);
				_m_cmd_stream << literal << ' ';
				return *this;
			}
			statement_builder& operator<<(const keyword& keyword_) {
				place_comma();
				_m_cmd_stream << keyword_.get_sql() << ' ';
				return *this;
			}
			statement_builder& operator<<(value&& variable) {
				place_comma();
				if (variable.has_id()) {
					assert(variable.get_id() + 1 < _m_id_limit && "Integer ID too high");
				} else {
					variable.set_id(_m_first_unused_id++);
				}
				if (variable.get_id() >= _m_val_map.size()) {
					_m_val_map.resize(variable.get_id() + 1);
				}
				_m_cmd_stream << '?' << (variable.get_id() + 1) << ' ';
				assert(!_m_val_map.at(variable.get_id()).has_value() || _m_val_map.at(variable.get_id())->get_type() == variable.get_type() &&
					   "Type mismatch with prior use of variable");
				assert(!_m_val_map.at(variable.get_id()).has_value() || _m_val_map.at(variable.get_id())->has_data() &&
					   "Cannot repeat the ID of a variable with a value");
				_m_val_map.at(variable.get_id()) = std::move(variable);
				return *this;
			}
			statement_builder& operator<<(sentinel sentinel) {
				switch (sentinel) {
				case sentinel::LEFT_PARENS:
					_m_comma_waiting.push_back(false);
					_m_cmd_stream << "( ";
					break;
				case sentinel::RIGHT_PARENS:
					assert(!_m_comma_waiting.empty() && "More right than left parens");
					_m_comma_waiting.pop_back();
					_m_cmd_stream << ") ";
					break;
				case sentinel::COMMA:
					assert(!_m_comma_waiting.empty() && "comma only allowed inside parens");
					_m_comma_waiting.pop_back();
					_m_comma_waiting.push_back(true);
					break;
				}
				return *this;
			}
			statement compile() &&;
		};

		class table {
		private:
			friend class database;
			std::reference_wrapper<database> _m_db;
			std::string _m_name;
			schema _m_schema;
			statement _m_insert_statement;

			statement create_insert_statement() {
				statement_builder builder {_m_db.get()};
				builder << keyword_INSERT_INTO << _m_name << keyword_VALUES << sentinel::LEFT_PARENS;
				for (const field& field : _m_schema.get_fields()) {
					builder << value::placeholder(field.get_type()) << sentinel::COMMA;
				}
				builder << sentinel::RIGHT_PARENS;
				return std::move(builder).compile();
			}

			table(database& db_, std::string&& name_, schema&& schema_)
				: _m_db{std::ref(db_)}
				, _m_name{std::move(name_)}
				, _m_schema{std::move(schema_)}
				, _m_insert_statement{create_insert_statement()}
			{ }

		public:

			const database& get_db() const { return _m_db; }

			const schema& get_schema() const { return _m_schema; }

			void bulk_insert(std::vector<std::vector<value>>&& rows) {
				// https://stackoverflow.com/questions/1711631/improve-insert-per-second-performance-of-sqlite

				_m_db.get().begin_transaction();
				for (std::vector<value>& row : rows) {
					for (size_t i = 0; i < row.size(); ++i) {
						row.at(i).set_id(i);
						assert(row.at(i).get_type() == _m_schema.get_fields().at(i).get_type());
						_m_insert_statement.set(std::move(row.at(i)));
					}
					_m_insert_statement.step();
					_m_insert_statement.reset();
				}
				_m_db.get().end_transaction();
			}

			void insert(std::vector<value>&& row) {
				for (size_t i = 0; i < row.size(); ++i) {
					row.at(i).set_id(i);
					assert(row.at(i).get_type() == _m_schema.get_fields().at(i).get_type());
					_m_insert_statement.set(std::move(row.at(i)));
				}
				_m_insert_statement.step();
				_m_insert_statement.reset();
			}

		};

		[[maybe_unused]] static std::ostream& operator<<(std::ostream& os, const table& table) {
			return os << "table{" << table.get_db() << ", " << table.get_schema() << "}";
		}

		inline table database::create_table(std::string&& name, schema&& schema) {
				statement_builder statement_builder {*this};
				statement_builder << keyword_CREATE_TABLE << name << sentinel::LEFT_PARENS;
				for (const field& field : schema.get_fields()) {
					statement_builder << field.get_name() << field.get_type().get_name() << sentinel::COMMA;
				}
				statement_builder << sentinel::RIGHT_PARENS;
				std::move(statement_builder).compile().step();
				return table{*this, std::move(name), std::move(schema)};
			}

		inline statement statement_builder::compile() && {
				assert(_m_comma_waiting.empty() && "More left parens than right parens");
				for (size_t i = 0; i < _m_val_map.size(); ++i) {
					assert(_m_val_map.at(i).has_value() && "Not all integer IDs are used.");
				}
				_m_cmd_stream << ";";
				std::vector<value> val_map;
				val_map.reserve(_m_val_map.size());
				for (size_t i = 0; i < _m_val_map.size(); ++i) {
					val_map.emplace_back(std::move(*_m_val_map.at(i)));
				}
				return statement{_m_db, _m_cmd_stream.str(), std::move(val_map)};
			}

		inline void database::begin_transaction() {
			assert(!in_transaction);
			in_transaction = true;
			begin_transaction_statement.step();
		}
		inline void database::end_transaction() {
			assert(in_transaction);
			in_transaction = false;
			end_transaction_statement.step();
		}

		inline statement::statement(database& db, std::string&& cmd, std::vector<value>&& val_map)
				: _m_val_map{std::move(val_map)}
				, _m_stmt{nullptr}
			{
				int rc = sqlite3_prepare_v2(db._m_db.get(), cmd.c_str(), cmd.size(), &_m_stmt, nullptr);
				sqlite_error_to_exception(rc, "prepare stmt", cmd);

				for (auto& value : _m_val_map) {
					if (value.has_data()) {
						set(std::move(value));
					}
				}
			}

		inline database::database(std::string&& url)
				: _m_url{std::move(url)}
				, _m_db{open_db(_m_url.c_str())}
				, in_transaction{false}
				, begin_transaction_statement{std::move(statement_builder{*this} << keyword_BEGIN_TRANSACTION).compile()}
				, end_transaction_statement{std::move(statement_builder{*this} << keyword_END_TRANSACTION).compile()}
			{ }
	}
}
