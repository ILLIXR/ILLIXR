#include "common/logging.hpp"

using namespace ILLIXR;

class noop_logger : public c_metric_logger {
public:
  /**
   * @brief Writes one log record.
   */
  virtual void log(const record& r) {
	  r.mark_used();
  }
};
