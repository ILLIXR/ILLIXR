#include "illixr/record_logger.hpp"

namespace ILLIXR {
class noop_record_logger : public record_logger {
protected:
    virtual void log(const record& r) override {
        r.mark_used();
    }
};
} // namespace ILLIXR
