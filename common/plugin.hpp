#pragma once

#include "phonebook.hpp"
#include "record_logger.hpp"
#include "init_protocol.hpp"

namespace ILLIXR {

	/*
	 * This gets included, but it is functionally 'private'. Hence the double-underscores.
	 */
	const record_header __plugin_start_header {
		"plugin_name",
		{
			{"plugin_id", typeid(std::size_t)},
			{"plugin_name", typeid(std::string)},
		},
	};

	/**
	 * @brief A dynamically-loadable plugin for Spindle.
	 *
	 * plugin RequiresInitProtocol so that threadloop, so that clients can make init/deinit methods
	 * which run after constructors and before destructors.
	 */
	class plugin : public RequiresInitProtocol<plugin> {
	public:

		plugin(const std::string& name_, phonebook* pb_)
			: name{name_}
			, pb{pb_}
			, record_logger_{pb->lookup_impl<record_logger>()}
			, gen_guid_{pb->lookup_impl<gen_guid>()}
			, id{gen_guid_->get()}
		{
			record_logger_->log(record{__plugin_start_header, {
				{id},
				{name},
			}});
		}

		virtual ~plugin() { }

		std::string get_name() { return name; }

	protected:
		std::string name;
		const phonebook* pb;
		const std::shared_ptr<record_logger> record_logger_;
		const std::shared_ptr<gen_guid> gen_guid_;
		const std::size_t id;
	};

#define PLUGIN_MAIN(PluginClass)										\
    extern "C" plugin* this_plugin_factory(phonebook* pb) {				\
        auto* obj =	new ProvidesInitProtocol<PluginClass> {#PluginClass, pb}; \
        return obj;														\
    }
}
