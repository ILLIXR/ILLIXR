#include "common/logging.hpp"

using namespace ILLIXR;

class noop_logger : public c_metric_logger {
public:
  /**
   * @brief Writes one log record.
   */
  virtual void log(const record& r) {}

  /**
   * @brief Writes many of the same type of log record.
   *
   * This is more efficient than calling log many times.
   */
  virtual void log(const std::vector<record>& r) {}
};
