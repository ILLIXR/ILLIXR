#pragma once

#include <iterator>
#include <sstream>
#include <string>
#include <vector>

class data_row {
public:
    data_row(char delim) : delimiter(delim) {}

    std::string const& operator[](std::size_t index) const {
        return data_[index];
    }

    [[nodiscard]] std::size_t size() const {
        return data_.size();
    }

    void read_next_row(std::istream& str) {
        std::string line;
        std::getline(str, line);

        line = line.substr(0, line.find_last_not_of("\r\n\t \v") + 1);

        std::stringstream line_stream(line);
        std::string       cell;

        data_.clear();
        while (std::getline(line_stream, cell, delimiter)) {
            data_.push_back(cell);
        }
        // This checks for a trailing comma with no data after it.
        if (!line_stream && cell.empty()) {
            // If there was a trailing comma, then add an empty element.
            data_.emplace_back("");
        }
    }

private:
    std::vector<std::string> data_;
    const char delimiter;
};

std::istream& operator>>(std::istream& str, data_row& data) {
    data.read_next_row(str);
    return str;
}

class data_iterator {
public:
    typedef std::input_iterator_tag iterator_category;
    typedef data_row                 value_type;
    typedef std::size_t             difference_type;
    typedef data_row*                pointer;
    typedef data_row&                reference;

    explicit data_iterator(std::istream& str, std::size_t skip = 0, char delimiter = ',')
        : stream_(str.good() ? &str : nullptr)
        , row_{delimiter} {
        ++(*this);
        (*this) += skip;
    }

    data_iterator(char delimiter = ',')
        : stream_(nullptr)
        , row_(delimiter){ }

    data_iterator& operator+=(std::size_t skip) {
        for (size_t i = 0; i < skip; ++i) {
            ++(*this);
        }
        return *this;
    }

    // Pre Increment
    data_iterator& operator++() {
        if (stream_) {
            if (!((*stream_) >> row_)) {
                stream_ = nullptr;
            }
        }
        return *this;
    }

    // Post increment
    data_iterator operator++(int) {
        data_iterator tmp(*this);
        ++(*this);
        return tmp;
    }

    data_row const& operator*() const {
        return row_;
    }

    data_row const* operator->() const {
        return &row_;
    }

    bool operator==(data_iterator const& rhs) {
        return ((this == &rhs) || ((this->stream_ == nullptr) && (rhs.stream_ == nullptr)));
    }

    bool operator!=(data_iterator const& rhs) {
        return !((*this) == rhs);
    }

    const std::string& operator[](std::size_t idx) {
        return row_[idx];
    }

private:
    std::istream* stream_;
    data_row       row_;
};
