#pragma once

#include "illixr/record_logger.hpp"

namespace ILLIXR {
class no_op_record_logger : public record_logger {
protected:
    void log(const record&r) override {
        r.mark_used();
    }
};
}