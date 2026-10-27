#include "illixr/data_format/serialization/openxr_payloads.hpp"

#include <boost/archive/binary_iarchive.hpp>
#include <boost/archive/binary_oarchive.hpp>

// Add lines below for new data types
IXR_EVENT_EXPORT_IMPLEMENT(ixr_foo_data)

// empty function, used to ensure the linker does not drop this obj when creating a static library
namespace ILLIXR {
void register_ixr_serialization() {}
}