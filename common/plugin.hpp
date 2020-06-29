#ifndef PLUGIN_HH
#define PLUGIN_HH

#include "phonebook.hpp"

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
		 *
		 * There is no `stop()` because destructor should be considered analagous.
		 */
		virtual void start() { };

		const std::string& get_name() { return name; }

		plugin(const std::string& name_, phonebook* pb_)
			: pb{pb_}
			, name{name_}
		{ }

		virtual ~plugin() { }

	protected:
		const phonebook* pb;

	private:
		const std::string name;
	};

#define PLUGIN_MAIN(plugin_class)                                    \
    extern "C" plugin* plugin_main(phonebook* pb) {                  \
        plugin_class* obj = new plugin_class {#plugin_class, pb};    \
        obj->start();                                                \
        return obj;                                                  \
    }
}

#endif
