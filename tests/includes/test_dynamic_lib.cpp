/**
 * @brief Unit tests for dynamic_lib (include/illixr/dynamic_lib.hpp).
 */

#include "illixr/dynamic_lib.hpp"

#include <gtest/gtest.h>
#include <stdexcept>

namespace ILLIXR {

namespace {

TEST(DynamicLibTest, CreateThrowsOnNonexistentPath) {
    ASSERT_THROW(dynamic_lib::create(std::string("/this/path/definitely/does/not/exist.so")), std::runtime_error);
}

#ifdef __linux__
// libc is always present on any Linux system this could run on, so it's used here as a
// known-good, always-loadable library for exercising the success path.
TEST(DynamicLibTest, CreateSucceedsAndSymbolLookupWorks) {
    dynamic_lib lib       = dynamic_lib::create(std::string("libc.so.6"));
    auto        malloc_fn = lib.get<void* (*) (size_t)>("malloc");
    void*       p         = malloc_fn(16);
    ASSERT_NE(p, nullptr);
    auto free_fn = lib.get<void (*)(void*)>("free");
    free_fn(p);
}

TEST(DynamicLibTest, GetThrowsOnMissingSymbol) {
    dynamic_lib lib = dynamic_lib::create(std::string("libc.so.6"));
    ASSERT_THROW(lib.get<void (*)()>("this_symbol_does_not_exist_anywhere"), std::runtime_error);
}

TEST(DynamicLibTest, MoveConstructionTransfersHandle) {
    dynamic_lib lib1 = dynamic_lib::create(std::string("libc.so.6"));
    dynamic_lib lib2 = std::move(lib1);
    auto        malloc_fn = lib2.get<void* (*) (size_t)>("malloc");
    ASSERT_NE(malloc_fn, nullptr);
}
#elif defined(_WIN32) || defined(_WIN64)
// kernel32.dll is always loaded in every Windows process, so it's used here as a known-good,
// always-loadable library for exercising the success path.
TEST(DynamicLibTest, CreateSucceedsAndSymbolLookupWorks) {
    dynamic_lib lib        = dynamic_lib::create(std::string("kernel32.dll"));
    auto        get_pid_fn = lib.get<DWORD(WINAPI*)()>("GetCurrentProcessId");
    ASSERT_NE(get_pid_fn, nullptr);
    DWORD pid = get_pid_fn();
    ASSERT_GT(pid, 0u);
}

TEST(DynamicLibTest, GetThrowsOnMissingSymbol) {
    dynamic_lib lib = dynamic_lib::create(std::string("kernel32.dll"));
    ASSERT_THROW(lib.get<void(WINAPI*)()>("this_symbol_does_not_exist_anywhere"), std::runtime_error);
}

TEST(DynamicLibTest, MoveConstructionTransfersHandle) {
    dynamic_lib lib1       = dynamic_lib::create(std::string("kernel32.dll"));
    dynamic_lib lib2       = std::move(lib1);
    auto        get_pid_fn = lib2.get<DWORD(WINAPI*)()>("GetCurrentProcessId");
    ASSERT_NE(get_pid_fn, nullptr);
}
#endif
} // namespace

} // namespace ILLIXR
