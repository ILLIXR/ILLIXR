#pragma once

#include "phonebook.hpp"
#include "logging.hpp"

namespace ILLIXR {

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
			metric_logger->log(std::make_unique<const component_start_record>(id, name));
		}

		/**
		 * @brief A method which Spindle calls when it starts the component.
		 * 
		 * This is necessary for stop-actions which have to replaced by the subclass. Destructors
		 * would prepend instead of replace actions.
		 */
		virtual void stop() {
			metric_logger->log(std::make_unique<const component_stop_record>(id));
		}

		plugin(const std::string& name_, phonebook* pb_)
			: name{name_}
			, pb{pb_}
			, metric_logger{pb->lookup_impl<c_metric_logger>()}
			, gen_guid{pb->lookup_impl<c_gen_guid>()}
			, id{gen_guid->get()}
		{ }

		virtual ~plugin() { stop(); }

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
