#include "illixr/switchboard.hpp"

namespace ILLIXR {
void switchboard::topic::deserialize_and_put(std::vector<char>& buffer){
    boost::iostreams::stream<boost::iostreams::array_source> stream{buffer.data(), buffer.size()};
    cereal::BinaryInputArchive ia{stream};
    ptr<event> this_event;
    ia >> this_event;
    put(std::move(this_event));
}
void switchboard::network_writer::put(ptr<event>&& this_specific_event) {
    if (_m_backend->is_topic_networked(this->_m_topic.name())) {
        auto base_event = std::dynamic_pointer_cast<event>(std::move(this_specific_event));
        assert(base_event && "Event is not derived from switchboard::event");

        std::stringstream stream;
        cereal::BinaryOutputArchive oa(stream);
        oa << base_event;
        // convert to buffer
        _m_backend->topic_send(this->_m_topic.name(), stream.str());
    } else {
        writer<event>::put(std::move(this_specific_event));
    }
}
}