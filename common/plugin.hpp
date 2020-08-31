#pragma once

#include "phonebook.hpp"
#include "record_logger.hpp"

namespace ILLIXR {

	const record_header __plugin_start_header {
		"plugin_name",
		{
			{"plugin_id", typeid(std::size_t)},
			{"plugin_name", typeid(std::string)},
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
			record_logger_->log(record{&__plugin_start_header, {
				{id},
				{name},
			}});
		}

		/**
		 * @brief A method which Spindle calls when it starts the component.
		 * 
		 * This is necessary for stop-actions which have to replaced by the subclass. Destructors
		 * would prepend instead of replace actions.
		 */
		virtual void stop() { }

		plugin(const std::string& name_, phonebook* pb_)
			: name{name_}
			, pb{pb_}
			, record_logger_{pb->lookup_impl<record_logger>()}
			, gen_guid_{pb->lookup_impl<gen_guid>()}
			, id{gen_guid_->get()}
		{ }

		virtual ~plugin() { stop(); }

		std::string get_name() { return name; }

	protected:
		std::string name;
		const phonebook* pb;
		const std::shared_ptr<record_logger> record_logger_;
		const std::shared_ptr<gen_guid> gen_guid_;
		const std::size_t id;
	};

#define PLUGIN_MAIN(plugin_class)                                   \
    extern "C" plugin* this_plugin_factory(phonebook* pb) {         \
        plugin_class* obj = new plugin_class {#plugin_class, pb};   \
        obj->start();                                               \
        return obj;                                                 \
    }
}
