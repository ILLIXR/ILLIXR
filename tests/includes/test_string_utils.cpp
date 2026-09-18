/**
 * @brief Unit tests for string_utils (include/illixr/string_utils.hpp).
 */

#include "illixr/string_utils.hpp"

#include <gtest/gtest.h>

namespace ILLIXR {

namespace {

TEST(SplitTest, EmptyStringGivesNoTokens) {
    ASSERT_TRUE(split("", ',').empty());
}

TEST(SplitTest, SplitsOnDelimiter) {
    auto tokens = split("a,b,c", ',');
    ASSERT_EQ(tokens.size(), 3u);
    EXPECT_EQ(tokens[0], "a");
    EXPECT_EQ(tokens[1], "b");
    EXPECT_EQ(tokens[2], "c");
}

TEST(SplitTest, NoDelimiterPresentGivesWholeStringAsOneToken) {
    auto tokens = split("no_delimiter_here", ',');
    ASSERT_EQ(tokens.size(), 1u);
    EXPECT_EQ(tokens[0], "no_delimiter_here");
}

TEST(SplitTest, ConsecutiveDelimitersProduceEmptyTokens) {
    auto tokens = split("a,,b", ',');
    ASSERT_EQ(tokens.size(), 3u);
    EXPECT_EQ(tokens[0], "a");
    EXPECT_EQ(tokens[1], "");
    EXPECT_EQ(tokens[2], "b");
}

TEST(SplitTest, LeadingDelimiterProducesLeadingEmptyToken) {
    auto tokens = split(",a", ',');
    ASSERT_EQ(tokens.size(), 2u);
    EXPECT_EQ(tokens[0], "");
    EXPECT_EQ(tokens[1], "a");
}

TEST(SplitTest, TrailingDelimiterDoesNotProduceTrailingEmptyToken) {
    // Verified against a compiled reproduction: std::getline hits EOF with nothing left to
    // extract after the trailing delimiter, so no final empty token is emitted.
    auto tokens = split("a,", ',');
    ASSERT_EQ(tokens.size(), 1u);
    EXPECT_EQ(tokens[0], "a");
}

TEST(SplitTest, SingleDelimiterOnlyGivesOneEmptyToken) {
    auto tokens = split(",", ',');
    ASSERT_EQ(tokens.size(), 1u);
    EXPECT_EQ(tokens[0], "");
}

TEST(SplitTest, DifferentDelimiterCharacter) {
    auto tokens = split("a;b;c", ';');
    ASSERT_EQ(tokens.size(), 3u);
    EXPECT_EQ(tokens[1], "b");
}

} // namespace

} // namespace ILLIXR
