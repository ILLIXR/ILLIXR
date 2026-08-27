#pragma once

#include "illixr/data_format/latency_data.hpp"
#include "misc.hpp"

#include <boost/serialization/binary_object.hpp>

namespace boost::serialization {

template<class Archive>
[[maybe_unused]] void serialize(Archive& ar, ILLIXR::data_format::latency_ping& ping, const unsigned int version) {
    (void) version;
    ar& boost::serialization::base_object<ILLIXR::switchboard::event>(ping);
    ar & ping.sequence_number;
    ar & ping.client_timestamp_ns;
}

template<class Archive>
[[maybe_unused]] void serialize(Archive& ar, ILLIXR::data_format::latency_pong& pong, const unsigned int version) {
    (void) version;
    ar& boost::serialization::base_object<ILLIXR::switchboard::event>(pong);
    ar & pong.sequence_number;
    ar & pong.client_timestamp_ns;
    ar & pong.server_timestamp_ns;
}

} // namespace boost::serialization

BOOST_CLASS_EXPORT_KEY(ILLIXR::data_format::latency_ping)
BOOST_CLASS_EXPORT_KEY(ILLIXR::data_format::latency_pong)
