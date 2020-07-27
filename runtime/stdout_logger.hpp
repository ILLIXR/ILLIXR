#include <memory>
#include <iostream>
#include "common/logging.hpp"
#include "common/record_types.hpp"

namespace ILLIXR {


class stdout_metric_logger : public c_metric_logger {
protected:
	virtual void log2(const struct_type* ty, std::unique_ptr<const record>&& r_) override {
		if (ty->type_id == start_skip_iteration_record::type_descr.type_id) {
			return;
		}
		if (ty->type_id == stop_skip_iteration_record::type_descr.type_id) {
			return;
		}
		const char* r = reinterpret_cast<const char*>(r_.get());
		std::cout << "record:" << ty->name << ",";
		for (const auto& pair : ty->fields) {
			const std::string& name = pair.first;
			const type* type_ = pair.second;
			std::cout << name << ":";
			if (false) {
			} else if (type_->type_id == types::std__size_t.type_id) {
				std::cout << *reinterpret_cast<const std::size_t*>(r) << ',';
			} else if (type_->type_id == types::std__string.type_id) {
				std::cout << "\"" << *reinterpret_cast<const std::string*>(r) << "\",";
			} else if (type_->type_id == types::std__chrono__nanoseconds.type_id) {
				std::cout << reinterpret_cast<const std::chrono::nanoseconds*>(r)->count() << "ns,";
			} else {
				std::cout << "type(" << type_->name << "),";
			}
			r += type_->size;
		}
		std::cout << "\n";
	}
	virtual void log_many2(const struct_type* ty, std::vector<std::unique_ptr<const record>>&& rs) override {
		for (std::unique_ptr<const record>& r : rs) {
			log2(ty, std::move(r));
		}
	}
};
}
