#pragma once

#include "illixr/data_format/openxr_data.hpp"
#include "illixr/data_format/openxr_payloads.h"

#include <boost/archive/archive_exception.hpp>
#include <boost/serialization/base_object.hpp>
#include <boost/serialization/binary_object.hpp>
#include <boost/serialization/export.hpp>
#include <cstddef>
#include <cstdint>

#define IXR_EVENT_EXPORT_KEY(Payload)       BOOST_CLASS_EXPORT_KEY2(ixr_event<Payload>, "ixr_event<" #Payload ">")
#define IXR_EVENT_EXPORT_IMPLEMENT(Payload) BOOST_CLASS_EXPORT_IMPLEMENT(ixr_event<Payload>)

namespace ILLIXR {
void register_ixr_serialization();

/*
 * How an ixr_event's payload goes over the wire. The default sends the whole struct. Add an
 * overload for payloads with a variable amount of valid data; overloads must be declared above
 * the serialize function below, since it calls serialize_payload by qualified name.
 */
template<class Archive, class Payload>
[[maybe_unused]] void serialize_payload(Archive& ar, Payload& payload) {
    ar & boost::serialization::make_binary_object(&payload, sizeof(Payload));
}

/*
 * Sends the header fields and only the frame_count * channel_count valid samples. reserved is not
 * sent; on load it and the unused tail of samples are zero.
 */
template<class Archive>
[[maybe_unused]] void serialize_payload(Archive& ar, ixr_audio_chunk& chunk) {
    if constexpr (Archive::is_loading::value) {
        chunk = ixr_audio_chunk{};
    }

    ar & chunk.start_frame;
    ar & chunk.sample_rate;
    ar & chunk.channel_count;
    ar & chunk.frame_count;

    // Checked before the samples are read, so a corrupt or hostile peer can't overrun the array.
    if (chunk.frame_count > IXR_AUDIO_MAX_FRAMES || chunk.channel_count > IXR_AUDIO_MAX_CHANNELS) {
        throw boost::archive::archive_exception(boost::archive::archive_exception::other_exception,
                                                "ixr_audio_chunk frame or channel count out of range");
    }

    const std::size_t valid_bytes = static_cast<std::size_t>(chunk.frame_count) * chunk.channel_count * sizeof(float);
    ar & boost::serialization::make_binary_object(chunk.samples, valid_bytes);
}
} // namespace ILLIXR

namespace boost::serialization {

template<class Archive, class Payload>
[[maybe_unused]] void serialize(Archive& ar, ixr_event<Payload>& data, const unsigned int) {
    ar& boost::serialization::base_object<ILLIXR::switchboard::event>(data);
    ILLIXR::serialize_payload(ar, data.data);
    ar & data.timestamp_ns;
}

} // namespace boost::serialization

// Add lines below for new data types
IXR_EVENT_EXPORT_KEY(ixr_foo_data)
IXR_EVENT_EXPORT_KEY(ixr_audio_chunk)
