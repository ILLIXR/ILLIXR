/**
 * @brief Unit tests for csv_iterator (include/illixr/iterators/csv_iterator.hpp).
 */

#include "illixr/iterators/csv_iterator.hpp"

#include <gtest/gtest.h>
#include <sstream>

namespace ILLIXR {

namespace {

TEST(CsvIteratorTest, SplitsOnCommas) {
    std::istringstream     iss{"1,2,3\n"};
    iterator::csv_iterator it{iss};
    ASSERT_EQ((*it).size(), 3u);
    EXPECT_EQ((*it)[0], "1");
    EXPECT_EQ((*it)[1], "2");
    EXPECT_EQ((*it)[2], "3");
}

TEST(CsvIteratorTest, DefaultConstructedIsTheEndSentinel) {
    iterator::csv_iterator end_it;
    std::istringstream     iss{"a,b\n"};
    iterator::csv_iterator it{iss};
    ++it; // only one row in the input
    ASSERT_EQ(it, end_it);
}

TEST(CsvIteratorTest, DoesNotSplitOnSpaces) {
    std::istringstream     iss{"one two,three\n"};
    iterator::csv_iterator it{iss};
    ASSERT_EQ((*it).size(), 2u);
    EXPECT_EQ((*it)[0], "one two");
    EXPECT_EQ((*it)[1], "three");
}

} // namespace

} // namespace ILLIXR
