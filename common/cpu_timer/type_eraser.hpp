#pragma once

#include <memory>

namespace cpu_timer {
namespace detail {
	// In C++17, consider using std::any
	using TypeEraser = std::shared_ptr<void>;

	static const TypeEraser type_eraser_default = TypeEraser{};

	// The following functions must be injected into cpu_timer namespace directly
	// so I will hold them in main instead.
	// template <typename T>
	// TypeEraser make_type_eraser(T* ptr)

	// template <typename T, class... Args>
	// TypeEraser make_type_eraser(Args&&... args)

	// template <typename T>
	// const T& extract_type_eraser(const TypeEraser& type_eraser)

	// template <typename T>
	// T& extract_type_eraser(TypeEraser& type_eraser)

} // namsepace detail
} // namespace cpu_timer
