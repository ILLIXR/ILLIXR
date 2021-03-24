#pragma once
#include <string>
#include "../runtime/sqlite_wrapper.hpp"

namespace ILLIXR {

	static const sqlite::schema site_info_schema {{
		{"plugin", sqlite::type_TEXT},
		{"topic_name", sqlite::type_TEXT},
		{"serial_no", sqlite::type_INTEGER},
		{"time", sqlite::type_INTEGER},
	}};

	class FrameInfo {
	private:
		std::string plugin;
		std::string topic;
		int64_t serial_no;
		int64_t time;
	public:
		FrameInfo(std::string plugin_ = std::string{}, std::string topic_ = std::string{}, int64_t serial_no_ = 0, int64_t time_ = 0)
			: plugin{plugin_}
			, topic{std::move(topic_)}
			, serial_no{serial_no_}
			, time{time_}
		{ }
		void serialize(std::vector<sqlite::value>& row) const {
			row.emplace_back(plugin);
			row.emplace_back(std::string_view{topic});
			row.emplace_back(serial_no);
			row.emplace_back(time);
		}
	};

	static const FrameInfo default_frame_info;
}
