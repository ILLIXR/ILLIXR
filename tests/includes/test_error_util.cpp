/**
 * @brief Unit tests for error_util (include/illixr/error_util.hpp).
 */

#include "illixr/error_util.hpp"

#include <cerrno>
#include <gtest/gtest.h>

namespace ILLIXR {

namespace {

TEST(ErrorUtilTest, ReportAndClearErrnoClearsASetErrno) {
    errno = EINVAL;
    report_and_clear_errno(__FILE__, __LINE__, __func__, "test message");
    ASSERT_EQ(errno, 0);
}

TEST(ErrorUtilTest, ReportAndClearErrnoIsANoOpWhenErrnoIsAlreadyZero) {
    errno = 0;
    report_and_clear_errno(__FILE__, __LINE__, __func__);
    ASSERT_EQ(errno, 0);
}

TEST(ErrorUtilTest, AbortTerminatesTheProcess) {
    // abort()'s behavior depends on NDEBUG: SIGABRT in debug builds, a plain exit in release.
#ifdef NDEBUG
    EXPECT_EXIT({ ILLIXR::abort("test abort", 1); }, ::testing::ExitedWithCode(1), "");
#else
    EXPECT_EXIT({ ILLIXR::abort("test abort"); }, ::testing::KilledBySignal(SIGABRT), "");
#endif
}

} // namespace

} // namespace ILLIXR
