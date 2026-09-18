/**
 * @brief Unit tests for global_module_defs (include/illixr/global_module_defs.hpp).
 */

#include "illixr/global_module_defs.hpp"

#include <gtest/gtest.h>
#include <stdexcept>

namespace ILLIXR {

namespace {

TEST(GlobalModuleDefsTest, StrToBoolParsesTrueVariants) {
    ASSERT_TRUE(str_to_bool("TRUE"));
    ASSERT_TRUE(str_to_bool("true"));
    ASSERT_TRUE(str_to_bool("True"));
    ASSERT_TRUE(str_to_bool("1"));
}

TEST(GlobalModuleDefsTest, StrToBoolParsesFalse) {
    ASSERT_FALSE(str_to_bool("FALSE"));
    ASSERT_FALSE(str_to_bool("false"));
}

TEST(GlobalModuleDefsTest, StrToBoolEmptyStringIsFalse) {
    ASSERT_FALSE(str_to_bool(""));
}

TEST(GlobalModuleDefsTest, StrToBoolRejectsZeroDespiteAcceptingOne) {
    // Asymmetric on purpose per the current implementation: "1" is accepted as true, but "0" is
    // not recognized as false and falls through to the error case.
    ASSERT_THROW(str_to_bool("0"), std::runtime_error);
}

TEST(GlobalModuleDefsTest, StrToBoolThrowsOnGarbage) {
    ASSERT_THROW(str_to_bool("not_a_bool"), std::runtime_error);
}

TEST(GlobalModuleDefsTest, DisplayParamsAreSane) {
    ASSERT_GT(display_params::width_pixels, 0u);
    ASSERT_GT(display_params::height_pixels, 0u);
    ASSERT_GT(display_params::frequency, 0.0f);
    ASSERT_GT(display_params::period.count(), 0);
}

} // namespace

} // namespace ILLIXR
