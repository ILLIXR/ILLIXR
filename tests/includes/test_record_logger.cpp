/**
 * @brief Unit tests for record_logger (include/illixr/record_logger.hpp).
 */

#include "illixr/record_logger.hpp"

#include <gtest/gtest.h>
#include <string>
#include <thread>
#include <vector>

namespace ILLIXR {

namespace {

class mock_record_logger : public record_logger {
public:
    using record_logger::log; // otherwise log(const record&) below hides log(const vector<record>&)

    void log(const record& r) override {
        logged_count_++;
        r.mark_used();
    }

    int logged_count_ = 0;
};

record_header make_test_header() {
    return record_header{"test_record", {{"value", typeid(int)}}};
}

TEST(RecordHeaderTest, EqualityComparesNameAndColumns) {
    record_header a = make_test_header();
    record_header b = make_test_header();
    ASSERT_EQ(a, b);
}

TEST(RecordHeaderTest, DifferentNamesAreNotEqual) {
    record_header a{"name_a", {{"value", typeid(int)}}};
    record_header b{"name_b", {{"value", typeid(int)}}};
    ASSERT_NE(a, b);
}

TEST(RecordHeaderTest, AccessorsReturnConstructorValues) {
    record_header rh = make_test_header();
    ASSERT_EQ(rh.get_name(), "test_record");
    ASSERT_EQ(rh.get_columns(), 1u);
    ASSERT_EQ(rh.get_column_name(0), "value");
    ASSERT_EQ(rh.get_column_type(0), typeid(int));
}

TEST(RecordTest, GetValueReturnsConstructedValue) {
    record_header rh = make_test_header();
    record        r{rh, {std::any{42}}};
    ASSERT_EQ(r.get_value<int>(0), 42);
}

TEST(RecordLoggerTest, MockLoggerReceivesLoggedRecords) {
    mock_record_logger logger;
    record_header      rh = make_test_header();
    record             r{rh, {std::any{7}}};
    logger.log(r);
    ASSERT_EQ(logger.logged_count_, 1);
}

TEST(RecordLoggerTest, DefaultVectorLogCallsSingleLogForEach) {
    mock_record_logger   logger;
    record_header        rh = make_test_header();
    std::vector<record>  records;
    records.emplace_back(rh, std::vector<std::any>{std::any{1}});
    records.emplace_back(rh, std::vector<std::any>{std::any{2}});
    logger.log(records);
    ASSERT_EQ(logger.logged_count_, 2);
}

TEST(RecordCoalescerTest, FlushSendsBufferedRecordsToTheLogger) {
    auto              logger = std::make_shared<mock_record_logger>();
    record_header     rh     = make_test_header();
    record_coalescer  coalescer{logger};

    coalescer.log(record{rh, {std::any{1}}});
    ASSERT_EQ(logger->logged_count_, 0); // not flushed yet

    coalescer.flush();
    ASSERT_EQ(logger->logged_count_, 1);
}

TEST(RecordCoalescerTest, DestructorFlushesRemainingRecords) {
    auto          logger = std::make_shared<mock_record_logger>();
    record_header rh     = make_test_header();
    {
        record_coalescer coalescer{logger};
        coalescer.log(record{rh, {std::any{1}}});
    }
    ASSERT_EQ(logger->logged_count_, 1);
}

TEST(RecordCoalescerTest, MaybeFlushTriggersAfterBufferDelayElapses) {
    auto original_delay = LOG_BUFFER_DELAY;
    LOG_BUFFER_DELAY    = std::chrono::milliseconds{5};

    auto             logger = std::make_shared<mock_record_logger>();
    record_header    rh     = make_test_header();
    record_coalescer coalescer{logger};

    coalescer.log(record{rh, {std::any{1}}});
    std::this_thread::sleep_for(std::chrono::milliseconds{10});
    coalescer.log(record{rh, {std::any{2}}}); // triggers maybe_flush() internally

    ASSERT_GE(logger->logged_count_, 1);

    LOG_BUFFER_DELAY = original_delay;
}

TEST(RecordCoalescerTest, OperatorBoolReflectsWhetherLoggerIsSet) {
    record_coalescer with_logger{std::make_shared<mock_record_logger>()};
    ASSERT_TRUE(static_cast<bool>(with_logger));

    record_coalescer without_logger{nullptr};
    ASSERT_FALSE(static_cast<bool>(without_logger));
}

} // namespace

} // namespace ILLIXR
