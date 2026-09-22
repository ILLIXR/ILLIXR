/**
 * @brief Unit tests for data_row and data_iterator (include/illixr/iterators/data_iterator.hpp).
 *
 * data_row and data_iterator are declared at global scope (no namespace ILLIXR wrapper), so
 * they're referenced here unqualified even though this test code lives in namespace ILLIXR,
 * matching the project convention used everywhere else in this file.
 */

#include "illixr/iterators/data_iterator.hpp"

#include <gtest/gtest.h>
#include <sstream>
#include <string>
#include <vector>

namespace ILLIXR {

namespace {

TEST(DataRowTest, ParsesASimpleCommaSeparatedLine) {
    std::istringstream iss{"a,b,c"};
    iterator::data_row row{','};
    row.read_next_row(iss);
    ASSERT_EQ(row.size(), 3u);
    EXPECT_EQ(row[0], "a");
    EXPECT_EQ(row[1], "b");
    EXPECT_EQ(row[2], "c");
}

TEST(DataRowTest, TrailingDelimiterProducesATrailingEmptyCell) {
    // Different from string_utils::split(), which does not produce a trailing empty token here --
    // verified against a standalone reproduction before writing this assertion.
    std::istringstream iss{"a,b,"};
    iterator::data_row row{','};
    row.read_next_row(iss);
    ASSERT_EQ(row.size(), 3u);
    EXPECT_EQ(row[2], "");
}

TEST(DataRowTest, ConsecutiveDelimitersProduceAnEmptyCellBetweenThem) {
    std::istringstream iss{"a,,b"};
    iterator::data_row row{','};
    row.read_next_row(iss);
    ASSERT_EQ(row.size(), 3u);
    EXPECT_EQ(row[1], "");
}

TEST(DataRowTest, ASingleDelimiterAloneProducesTwoEmptyCells) {
    // Also different from split(), which gives one empty token for a lone delimiter.
    std::istringstream iss{","};
    iterator::data_row row{','};
    row.read_next_row(iss);
    ASSERT_EQ(row.size(), 2u);
    EXPECT_EQ(row[0], "");
    EXPECT_EQ(row[1], "");
}

TEST(DataRowTest, AnEmptyLineProducesOneEmptyCellNotZero) {
    // Also different from split(""), which gives zero tokens.
    std::istringstream iss{""};
    iterator::data_row row{','};
    row.read_next_row(iss);
    ASSERT_EQ(row.size(), 1u);
    EXPECT_EQ(row[0], "");
}

TEST(DataRowTest, TrailingCarriageReturnIsTrimmed) {
    std::istringstream iss{"a,b,c\r"};
    iterator::data_row row{','};
    row.read_next_row(iss);
    ASSERT_EQ(row.size(), 3u);
    EXPECT_EQ(row[2], "c");
}

TEST(DataIteratorTest, ConstructorAdvancesToTheFirstRow) {
    std::istringstream      iss{"a,b\nc,d\n"};
    iterator::data_iterator it{iss};
    ASSERT_EQ((*it)[0], "a");
    ASSERT_EQ((*it)[1], "b");
}

TEST(DataIteratorTest, IncrementMovesToTheNextRow) {
    std::istringstream      iss{"a,b\nc,d\n"};
    iterator::data_iterator it{iss};
    ++it;
    ASSERT_EQ((*it)[0], "c");
    ASSERT_EQ((*it)[1], "d");
}

TEST(DataIteratorTest, ReachesTheEndSentinelAfterTheLastRow) {
    std::istringstream      iss{"a,b\n"};
    iterator::data_iterator it{iss};
    ++it;
    ASSERT_EQ(it, iterator::data_iterator{});
}

TEST(DataIteratorTest, SkipParameterSkipsAdditionalRows) {
    std::istringstream      iss{"a\nb\nc\nd\n"};
    iterator::data_iterator it{iss, 2}; // constructor advances once, then skips 2 more -> row "c"
    ASSERT_EQ((*it)[0], "c");
}

TEST(DataIteratorTest, OperatorBracketProxiesToTheCurrentRow) {
    std::istringstream      iss{"x,y,z\n"};
    iterator::data_iterator it{iss};
    ASSERT_EQ(it[1], "y");
}

TEST(DataIteratorTest, RangeForStyleLoopVisitsEveryRow) {
    std::istringstream       iss{"1,a\n2,b\n3,c\n"};
    std::vector<std::string> first_columns;
    for (iterator::data_iterator it{iss}; it != iterator::data_iterator{}; ++it) {
        first_columns.push_back((*it)[0]);
    }
    ASSERT_EQ(first_columns.size(), 3u);
    EXPECT_EQ(first_columns[0], "1");
    EXPECT_EQ(first_columns[1], "2");
    EXPECT_EQ(first_columns[2], "3");
}

} // namespace

} // namespace ILLIXR
