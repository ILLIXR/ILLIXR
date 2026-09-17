#pragma once

#include "../export.hpp"

namespace ILLIXR {

/**
 * @brief Marks a unit test shared library as belonging to a given ILLIXR component.
 *
 * Every unit test shared library built by add_illixr_unit_test() must invoke this macro exactly
 * once, in exactly one of its source files. It does not run any tests itself: GoogleTest's
 * TEST()/TEST_F() macros already register their tests into the shared, process-wide GoogleTest
 * registry the moment this library is loaded. this_test_component() instead gives the unit test
 * runner two things: a symbol it can dlsym to confirm a discovered shared library is really one
 * of ours (as opposed to some unrelated file sitting in the unit_tests output directory), and a
 * readable component name to attach to test results.
 */
#define UNIT_TEST_MAIN(COMPONENT_NAME)                             \
    extern "C" MY_EXPORT_API const char* this_test_component() {   \
        return COMPONENT_NAME;                                     \
    }

} // namespace ILLIXR
