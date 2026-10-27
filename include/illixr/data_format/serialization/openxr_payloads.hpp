#pragma once

#include "illixr/data_format/openxr_data.hpp"
#include "illixr/data_format/openxr_payloads.h"

#include <boost/serialization/base_object.hpp>
#include <boost/serialization/binary_object.hpp>
#include <boost/serialization/export.hpp>

#define IXR_EVENT_EXPORT_KEY(Payload)       BOOST_CLASS_EXPORT_KEY2(ixr_event<Payload>, "ixr_event<" #Payload ">")
#define IXR_EVENT_EXPORT_IMPLEMENT(Payload) BOOST_CLASS_EXPORT_IMPLEMENT(ixr_event<Payload>)

namespace ILLIXR {
void register_ixr_serialization();
}
namespace boost::serialization {

template<class Archive, class Payload>
[[maybe_unused]] void serialize(Archive& ar, ixr_event<Payload>& data, const unsigned int) {
    ar& boost::serialization::base_object<ILLIXR::switchboard::event>(data);
    ar & boost::serialization::make_binary_object(&data.data, sizeof(Payload));
    ar & data.timestamp_ns;
}
} // namespace boost::serialization

IXR_EVENT_EXPORT_KEY(ixr_foo_data)
