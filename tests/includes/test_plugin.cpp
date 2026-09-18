/**
 * @brief Unit tests for plugin (include/illixr/plugin.hpp).
 */

#include "illixr/phonebook.hpp"
#include "illixr/plugin.hpp"
#include "illixr/record_logger.hpp"

#include <gtest/gtest.h>
#include <memory>
#include <spdlog/spdlog.h>
#include <stdexcept>
#include <string>

namespace ILLIXR {

namespace {

/// plugin's constructor looks up a record_logger from the phonebook unconditionally; this stands
/// in for one, and captures what start() actually logs so it can be inspected.
class mock_record_logger : public record_logger {
public:
    using record_logger::log; // otherwise log(const record&) below hides log(const vector<record>&)

    void log(const record& r) override {
        logged_count_++;
        last_plugin_id_   = r.get_value<plugin_id_t>(0);
        last_plugin_name_ = r.get_value<std::string>(1);
        r.mark_used();
    }

    int         logged_count_ = 0;
    plugin_id_t last_plugin_id_{};
    std::string last_plugin_name_;
};

/// Drops a named spdlog logger at scope exit, so registering one in a test (via
/// plugin::spdlogger()) never leaks into whichever test the shuffled runner picks next.
class scoped_logger_cleanup {
public:
    explicit scoped_logger_cleanup(std::string name)
        : name_{std::move(name)} { }

    ~scoped_logger_cleanup() {
        spdlog::drop(name_);
    }

private:
    std::string name_;
};

class PluginTest : public ::testing::Test {
protected:
    void SetUp() override {
        pb_ = std::make_unique<phonebook>();
        pb_->register_impl<record_logger>(std::make_shared<mock_record_logger>());
    }

    std::unique_ptr<phonebook> pb_;
};

TEST_F(PluginTest, ConstructorStoresName) {
    plugin p{"my_plugin", pb_.get()};
    ASSERT_EQ(p.get_name(), "my_plugin");
}

TEST_F(PluginTest, EachPluginGetsADistinctId) {
    // id_ is protected with no accessor; the plugin_id logged by start() is the only externally
    // observable channel for it, so that's what's used here to confirm two plugins built from the
    // same phonebook don't collide (which also doubles as a regression check on get_next_id()).
    auto logger = std::static_pointer_cast<mock_record_logger>(pb_->lookup_impl<record_logger>());

#ifndef __ANDROID__
    plugin a{"plugin_a", pb_.get()};
    a.start();
    auto id_a = logger->last_plugin_id_;

    plugin b{"plugin_b", pb_.get()};
    b.start();
    auto id_b = logger->last_plugin_id_;

    ASSERT_NE(id_a, id_b);
#else
    GTEST_SKIP() << "start() only logs (and thus only exposes plugin_id) on non-Android.";
#endif
}

TEST_F(PluginTest, StartLogsAPluginStartRecord) {
    auto   logger = std::static_pointer_cast<mock_record_logger>(pb_->lookup_impl<record_logger>());
    plugin p{"my_plugin", pb_.get()};
    p.start();
#ifndef __ANDROID__
    ASSERT_EQ(logger->logged_count_, 1);
    ASSERT_EQ(logger->last_plugin_name_, "my_plugin");
#else
    ASSERT_EQ(logger->logged_count_, 0); // start() only logs on non-Android
#endif
}

TEST_F(PluginTest, StopWithoutASpdloggerDoesNotThrow) {
    plugin p{"my_plugin", pb_.get()};
    ASSERT_NO_THROW(p.stop());
}

TEST_F(PluginTest, SpdAddFileSinkThrowsBeforeSpdloggerIsCalled) {
    plugin p{"my_plugin", pb_.get()};
    ASSERT_THROW(p.spd_add_file_sink("extra", "log", "info"), std::runtime_error);
}

TEST_F(PluginTest, SpdloggerCreatesAndRegistersANamedLogger) {
    plugin                p{"test_plugin_spdlogger_create", pb_.get()};
    scoped_logger_cleanup cleanup{"test_plugin_spdlogger_create"};

    auto logger = p.spdlogger(nullptr);
    ASSERT_NE(logger, nullptr);
    ASSERT_EQ(spdlog::get("test_plugin_spdlogger_create"), logger);
}

TEST_F(PluginTest, SpdloggerReusesAnAlreadyRegisteredLoggerWithTheSameName) {
    plugin                p{"test_plugin_spdlogger_reuse", pb_.get()};
    scoped_logger_cleanup cleanup{"test_plugin_spdlogger_reuse"};

    auto first  = p.spdlogger(nullptr);
    auto second = p.spdlogger(nullptr);
    ASSERT_EQ(first, second);
}

TEST_F(PluginTest, SpdAddFileSinkAddsASinkAfterSpdloggerIsCalled) {
    plugin                p{"test_plugin_add_sink", pb_.get()};
    scoped_logger_cleanup cleanup{"test_plugin_add_sink"};

    auto logger       = p.spdlogger(nullptr);
    auto sinks_before = logger->sinks().size();
    p.spd_add_file_sink("extra", "log", "info");
    ASSERT_EQ(logger->sinks().size(), sinks_before + 1);
}

} // namespace

} // namespace ILLIXR
