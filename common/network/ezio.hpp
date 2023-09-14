/* -*-mode:c++; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*- */

#ifndef EZIO_HPP
#define EZIO_HPP

#include "exception.hpp"

#include <string>

namespace ILLIXR {

/* convert string to long integer */
long int myatoi(const std::string& str, const int base = 10) {
    if (str.empty()) {
        throw std::runtime_error("Invalid integer string: empty");
    }

    char* end;

    errno        = 0;
    long int ret = strtol(str.c_str(), &end, base);

    if (errno != 0) {
        throw unix_error("strtol");
    } else if (end != str.c_str() + str.size()) {
        throw std::runtime_error("Invalid integer: " + str);
    }

    return ret;
}

/* convert string to floating points */
double myatof(const std::string& str) {
    if (str.empty()) {
        throw std::runtime_error("Invalid floating-point string: empty");
    }

    char* end;

    errno      = 0;
    double ret = strtod(str.c_str(), &end);

    if (errno != 0) {
        throw unix_error("strtod");
    } else if (end != str.c_str() + str.size()) {
        throw std::runtime_error("Invalid floating-point number: " + str);
    }

    return ret;
}

} // namespace ILLIXR

#endif /* EZIO_HPP */
