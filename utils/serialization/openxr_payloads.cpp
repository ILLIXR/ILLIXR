#include "illixr/data_format/serialization/openxr_payloads.hpp"

#include <boost/archive/binary_iarchive.hpp>
#include <boost/archive/binary_oarchive.hpp>

IXR_EVENT_EXPORT_IMPLEMENT(ixr_foo_data)

namespace ILLIXR {
void register_ixr_serialization() {}
}