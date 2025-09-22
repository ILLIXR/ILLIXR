#pragma once

#include "data_iterator.hpp"


class ssv_iterator : public data_iterator{
public:

    explicit ssv_iterator(std::istream& str, std::size_t skip = 0)
        : data_iterator(str, skip, ' ') {}

    ssv_iterator()
        : data_iterator(' ') { }
};
