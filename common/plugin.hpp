#pragma once

#include "phonebook.hpp"
#include "logging.hpp"

namespace ILLIXR {

	const record_header __plugin_start_header {
		"plugin_start",
		{
			{"plugin_id", typeid(std::size_t)},
			{"plugin_name", typeid(std::string)},
			{"cpu_time", typeid(std::chrono::nanoseconds)},
			{"wall_time", typeid(std::chrono::high_resolution_clock::time_point)},
		},
	};
	const record_header __plugin_stop_header {
		"plugin_stop",
		{
			{"plugin_id", typeid(std::size_t)},
			{"cpu_time", typeid(std::chrono::nanoseconds)},
			{"wall_time", typeid(std::chrono::high_resolution_clock::time_point)},
		},
	};

	/**
	 * @brief A dynamically-loadable plugin for Spindle.
	 */
	class plugin {
	public:
		/**
		 * @brief A method which Spindle calls when it starts the component.
		 * 
		 * This is necessary for actions which have to be started after constructions, such as
		 * threads. These cannot be started in the constructor because virtual methods don't work in
		 * consturctors.
		 */
		virtual void start() {
			metric_logger->log(record{&__plugin_start_header, {
				{id},
				{name},
				{thread_cpu_time()},
				{std::chrono::high_resolution_clock::now()},
			}});
		}

		/**
		 * @brief A method which Spindle calls when it starts the component.
		 *
		 * This is necessary because the parent class might define some actions that need to be
		 * taken prior to destructing the derived class. For example, threadloop must halt and join
		 * the thread before the derived class can be safely destructed. However, the derived
		 * class's destructor is called before its parent (threadloop), so threadloop doesn't get a
		 * chance to join the thread before the derived class is destroyed, and the thread accesses
		 * freed memory. Instead, we call plugin->stop manually before destrying anything.
		 */
		virtual void stop() {
			metric_logger->log(record{&__plugin_stop_header, {
				{id},
				{thread_cpu_time()},
				{std::chrono::high_resolution_clock::now()},
			}});
		}

		plugin(const std::string& name_, phonebook* pb_)
			: name{name_}
			, pb{pb_}
			, metric_logger{pb->lookup_impl<c_metric_logger>()}
			, gen_guid{pb->lookup_impl<c_gen_guid>()}
			, id{gen_guid->get()}
		{ }

		virtual ~plugin() { }

		std::string get_name() { return name; }

	protected:
		std::string name;
		const phonebook* pb;
		const std::shared_ptr<c_metric_logger> metric_logger;
		const std::shared_ptr<c_gen_guid> gen_guid;
		const std::size_t id;
	};

#define PLUGIN_MAIN(plugin_class)                                   \
    extern "C" plugin* this_plugin_factory(phonebook* pb) {         \
        plugin_class* obj = new plugin_class {#plugin_class, pb};   \
        obj->start();                                               \
        return obj;                                                 \
    }
}
