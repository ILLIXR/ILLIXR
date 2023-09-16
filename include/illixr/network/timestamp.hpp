/* -*-mode:c++; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*- */

#ifndef TIMESTAMP_HPP
#define TIMESTAMP_HPP

#include "exception.hpp"

#include <cstdint>
#include <ctime>

namespace ILLIXR {

uint64_t raw_timestamp(void) {
    timespec ts;
    system_call("clock_gettime", clock_gettime(CLOCK_REALTIME, &ts));

    uint64_t millis = ts.tv_nsec / 1000000;
    millis += uint64_t(ts.tv_sec) * 1000;

    return millis;
}

uint64_t initial_timestamp(void) {
    static uint64_t initial_value = raw_timestamp();
    return initial_value;
}

uint64_t timestamp(void) {
    return raw_timestamp() - initial_timestamp();
}

} // namespace ILLIXR

#endif /* TIMESTAMP_HPP */
