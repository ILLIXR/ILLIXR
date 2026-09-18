/**
 * @brief Unit tests for phonebook (include/illixr/phonebook.hpp).
 */

#include "illixr/phonebook.hpp"
#include "illixr/tests/unit_test.hpp"

#include <gtest/gtest.h>
#include <memory>
#include <string>

namespace ILLIXR {

UNIT_TEST_MAIN("include")

namespace {

/// A trivial phonebook::service used to exercise register_impl()/lookup_impl()/has_impl().
class mock_service_a : public phonebook::service {
public:
    explicit mock_service_a(int value)
        : value_{value} { }

    int value_;
};

/// A second, distinct service type, used to confirm lookups are keyed by type, not insertion order.
class mock_service_b : public phonebook::service {
public:
    explicit mock_service_b(std::string label)
        : label_{std::move(label)} { }

    std::string label_;
};

/// A third service type that is never registered, used to test unregistered service
class mock_service_c : public phonebook::service {
public:
    explicit mock_service_c(std::string label)
        : label_{std::move(label)} { }

    std::string label_;
};

class PhonebookTest : public ::testing::Test {
protected:
    phonebook pb_;
};

TEST_F(PhonebookTest, HasImplFalseWhenUnregistered) {
    ASSERT_FALSE(pb_.has_impl<mock_service_a>());
}

TEST_F(PhonebookTest, RegisterThenHasImplTrue) {
    pb_.register_impl<mock_service_a>(std::make_shared<mock_service_a>(42));
    ASSERT_TRUE(pb_.has_impl<mock_service_a>());
}

TEST_F(PhonebookTest, RegisterThenLookupReturnsSameImpl) {
    auto impl = std::make_shared<mock_service_a>(42);
    pb_.register_impl<mock_service_a>(impl);

    std::shared_ptr<mock_service_a> looked_up = pb_.lookup_impl<mock_service_a>();
    ASSERT_EQ(looked_up, impl);
    ASSERT_EQ(looked_up->value_, 42);
}

TEST_F(PhonebookTest, LookupUnregisteredThrows) {
    // Debug builds throw std::runtime_error explicitly; release builds fall through to
    // unordered_map::at()'s std::out_of_range instead. Both derive from std::exception, so this holds either way.
    ASSERT_THROW(pb_.lookup_impl<mock_service_c>(), std::exception);
}

TEST_F(PhonebookTest, LookupIsKeyedByType) {
    pb_.register_impl<mock_service_a>(std::make_shared<mock_service_a>(1));
    pb_.register_impl<mock_service_b>(std::make_shared<mock_service_b>("hello"));

    ASSERT_TRUE(pb_.has_impl<mock_service_a>());
    ASSERT_TRUE(pb_.has_impl<mock_service_b>());

    ASSERT_EQ(pb_.lookup_impl<mock_service_a>()->value_, 1);
    ASSERT_EQ(pb_.lookup_impl<mock_service_b>()->label_, "hello");
}

TEST_F(PhonebookTest, GetNextIdStartsAtZeroAndIncrements) {
    ASSERT_EQ(pb_.get_next_id(), 0u);
    ASSERT_EQ(pb_.get_next_id(), 1u);
    ASSERT_EQ(pb_.get_next_id(), 2u);
}

} // namespace

} // namespace ILLIXR
