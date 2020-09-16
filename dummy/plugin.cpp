#include "common/plugin.hpp"

using namespace ILLIXR;

class dummy_service : public phonebook::service {
public:
	virtual ~dummy_service() override {
		std::cerr << "dummy_service::~dummy_service()" << std::endl;
	}
};

class dummy : public plugin {
public:
	dummy(const std::string& name, phonebook* pb)
    	: plugin{name, pb}
	{
		pb->register_impl<dummy_service>(std::make_shared<dummy_service>());
	}

	virtual ~dummy() override {
		std::cerr << "dummy::~dummy()" << std::endl;
	}
};

PLUGIN_MAIN(dummy);
