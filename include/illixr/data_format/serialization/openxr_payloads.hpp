#pragma once

#include "illixr/data_format/openxr_data.hpp"
#include "illixr/data_format/openxr_payloads.h"

#include <boost/serialization/array.hpp>
#include <boost/serialization/binary_object.hpp>
#include <boost/serialization/export.hpp>

namespace boost::serialization {

template<class Archive>
[[maybe_unused]] void serialize(Archive& ar, ixr_foo_data& data, const unsigned int) {
    ar & boost::serialization::make_array(data.values, 4);
    ar & data.count;
    ar & data.padding;
}

//  ----------- Define all struct serialization above here ----------------
template<class Archive, typename Payload>
[[maybe_unused]] void serialize(Archive& ar, ixr_event<Payload>& data, const unsigned int) {
    ar& boost::serialization::base_object<ILLIXR::switchboard::event>(data);
    ar & data.data;
    ar & data.timestamp_ns;
}
} // namespace boost::serialization

BOOST_CLASS_EXPORT_KEY(ixr_foo_data)