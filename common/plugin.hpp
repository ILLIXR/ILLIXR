#ifndef PLUGIN_HH
#define PLUGIN_HH

#include "phonebook.hpp"

namespace ILLIXR {

	class plugin : service {
	public:
		/* This is necessary for actions which have to be started
		   after constructions, such as threads.  These cannot be
		   started in the constructor because virtual methods don't
		   work in consturctors.*/
		virtual void start() {};
	};



#define PLUGIN_MAIN(plugin_class) \
	extern "C" plugin* plugin_main(phonebook* pb) {					 \
		plugin_class* obj = new plugin_class {pb};					 \
		obj->start();												 \
		return obj;													 \
	}
}

#endif
