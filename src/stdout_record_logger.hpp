#pragma once

#include "illixr/data_format.hpp"
#include "illixr/record_logger.hpp"

#include <chrono>
#include <iostream>
#include <sstream>

namespace ILLIXR {
class stdout_record_logger : public record_logger {
protected:
    virtual void log(const record& r) override {
        const record_header& rh = r.get_record_header();
        for (unsigned i = 0; i < rh.get_columns(); ++i) {
            std::cout << rh.get_column_name(i) << ',';
            if (false) {
            } else if (rh.get_column_type(i) == typeid(std::size_t)) {
                std::cout << r.get_value<std::size_t>(i);
            } else if (rh.get_column_type(i) == typeid(bool)) {
                std::cout << std::boolalpha << r.get_value<bool>(i);
            } else if (rh.get_column_type(i) == typeid(double)) {
                std::cout << r.get_value<double>(i);
            } else if (rh.get_column_type(i) == typeid(duration)) {
                auto val = r.get_value<duration>(i);
                std::cout << static_cast<long long>(std::chrono::nanoseconds{val}.count());
            } else if (rh.get_column_type(i) == typeid(time_point)) {
                auto val = r.get_value<time_point>(i).time_since_epoch();
                std::cout << static_cast<long long>(std::chrono::nanoseconds{val}.count());
            } else if (rh.get_column_type(i) == typeid(std::chrono::nanoseconds)) {
                auto val = r.get_value<std::chrono::nanoseconds>(i);
                std::cout << static_cast<long long>(std::chrono::nanoseconds{val}.count());
            } else if (rh.get_column_type(i) == typeid(std::chrono::high_resolution_clock::time_point)) {
                auto val = r.get_value<std::chrono::high_resolution_clock::time_point>(i).time_since_epoch();
                std::cout << static_cast<long long>(std::chrono::nanoseconds{val}.count());
            } else if (rh.get_column_type(i) == typeid(std::string)) {
                std::cout << r.get_value<std::string>(i);
            } else {
                std::ostringstream ss;
                ss << "type " << rh.get_column_type(i).name() << " (used in " << rh.get_name() << ") is not implemented.\n";
                throw std::runtime_error{ss.str()};
            }
            std::cout << ',';
        }
        std::cout << '\n';
    }
};
} // namespace ILLIXR
