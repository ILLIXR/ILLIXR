#pragma once

#include "data_iterator.hpp"

class csv_iterator : public data_iterator {
public:
    explicit csv_iterator(std::istream& str, std::size_t skip = 0)
        : data_iterator(str, skip, ',') { }

    csv_iterator()
        : data_iterator(',') { }
};
