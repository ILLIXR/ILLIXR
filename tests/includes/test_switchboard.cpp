/**
 * @brief Unit tests for switchboard (include/illixr/switchboard.hpp).
 *
 * get_network_writer()/network_writer are deliberately not covered here -- they need a mocked
 * network::network_backend (TCP/UDP), which is a large enough design surface to warrant its own
 * discussion rather than guessing at how it should be mocked.
 */

#include "illixr/phonebook.hpp"
#include "illixr/switchboard.hpp"
#include "illixr/network/network_backend.hpp"
#include "illixr/record_logger.hpp"

#include <algorithm>
#include <atomic>
#include <boost/serialization/export.hpp>
#include <chrono>
#include <cstdlib>
#include <gtest/gtest.h>
#include <memory>
#include <set>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

namespace ILLIXR {
struct int_type : switchboard::event {
    int value;

    int_type() : value{0} {}
    int_type(const int v) : value{v} {}
};
}

namespace boost::serialization {
template<class Archive>
void serialize(Archive& ar, ILLIXR::int_type& data, const unsigned int) {
    ar& boost::serialization::base_object<ILLIXR::switchboard::event>(data);
    ar & data.value;
}
}
BOOST_CLASS_EXPORT_KEY(ILLIXR::int_type)

BOOST_CLASS_EXPORT_IMPLEMENT(ILLIXR::int_type)

namespace ILLIXR {

namespace {

/// Saves and restores an environment variable's previous value around a test (see
/// test_data_loading.cpp for the same pattern).
class scoped_env_var {
public:
    scoped_env_var(std::string name, const std::string& value)
        : name_{std::move(name)} {
        const char* existing = std::getenv(name_.c_str());
        had_previous_value_  = existing != nullptr;
        if (had_previous_value_) {
            previous_value_ = existing;
        }
        setenv(name_.c_str(), value.c_str(), 1);
    }

    ~scoped_env_var() {
        if (had_previous_value_) {
            setenv(name_.c_str(), previous_value_.c_str(), 1);
        } else {
            unsetenv(name_.c_str());
        }
    }

    scoped_env_var(const scoped_env_var&)            = delete;
    scoped_env_var& operator=(const scoped_env_var&) = delete;

private:
    std::string name_;
    bool        had_previous_value_;
    std::string previous_value_;
};

/// Polls pred until it returns true or timeout elapses (see test_threadloop.cpp for the same
/// pattern) -- switchboard's schedule()/buffered_reader paths are serviced by a background
/// thread, so most of these need to wait rather than assert immediately.
template<typename Predicate>
bool wait_until(Predicate pred, std::chrono::milliseconds timeout) {
    auto deadline = std::chrono::steady_clock::now() + timeout;
    while (std::chrono::steady_clock::now() < deadline) {
        if (pred()) {
            return true;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds{1});
    }
    return pred();
}

// --- get_network_writer() / network_writer -------------------------------------------------

/// switchboard's constructor looks up a record_logger unconditionally when given a non-null
/// phonebook (required here since get_network_writer needs a real phonebook to look up the
/// backend from). See test_plugin.cpp for why log() needs `using record_logger::log;`.
class mock_record_logger : public record_logger {
public:
    using record_logger::log;

    void log(const record& r) override {
        r.mark_used();
    }
};

/// A minimal in-memory stand-in for network::tcp_backend: tracks what was asked of it rather than
/// doing any real networking.
class mock_tcp_backend : public network::tcp_backend {
public:
    void topic_create(std::string topic_name, network::topic_config& config) override {
        (void) config;
        created_topics_.push_back(topic_name);
    }

    bool is_topic_networked(std::string topic_name) override {
        return networked_topics_.count(topic_name) > 0;
    }

    void topic_send(std::string topic_name, std::string&& message) override {
        send_count_++;
        last_sent_topic_        = std::move(topic_name);
        last_sent_message_size_ = message.size();
    }

    void start_client() override { }
    void start_server() override { }

    [[nodiscard]] network::topic_config::TransportMethod transport_method() const override {
        return network::topic_config::TransportMethod::TCP;
    }

    /// Test hook: makes is_topic_networked() return true for this topic from now on.
    void mark_networked(const std::string& topic_name) {
        networked_topics_.insert(topic_name);
    }

    std::vector<std::string> created_topics_;
    std::set<std::string>    networked_topics_;
    int                      send_count_             = 0;
    std::string              last_sent_topic_;
    size_t                   last_sent_message_size_ = 0;
};

class SwitchboardTest : public ::testing::Test {
protected:
    void SetUp() override {
        sb_ = std::make_unique<switchboard>(nullptr);
    }

    std::unique_ptr<switchboard> sb_;
};

// --- construction -----------------------------------------------------------------------------

TEST(SwitchboardConstructionTest, NullPhonebookDisablesLoggingWithoutRequiringOne) {
    ASSERT_NO_THROW(switchboard sb(nullptr));
}

TEST(SwitchboardConstructionTest, NonNullPhonebookRequiresARecordLoggerToBeRegistered) {
    phonebook pb; // nothing registered
    ASSERT_THROW(switchboard sb(&pb), std::exception);
}

// --- environment variable accessors ------------------------------------------------------------

TEST_F(SwitchboardTest, SetEnvThenGetEnvReturnsTheSetValue) {
    sb_->set_env("MY_TEST_VAR", "hello");
    ASSERT_EQ(sb_->get_env("MY_TEST_VAR"), "hello");
}

TEST_F(SwitchboardTest, GetEnvReturnsDefaultWhenUnset) {
    ASSERT_EQ(sb_->get_env("SOME_VAR_NOT_SET_XYZ", "fallback"), "fallback");
}

TEST_F(SwitchboardTest, GetEnvFallsBackToTheRealEnvironmentVariable) {
    scoped_env_var env{"ILLIXR_TEST_SWITCHBOARD_REAL_ENV", "real_value"};
    ASSERT_EQ(sb_->get_env("ILLIXR_TEST_SWITCHBOARD_REAL_ENV"), "real_value");
}

TEST_F(SwitchboardTest, GetEnvBoolParsesIntegerAndWordForms) {
    sb_->set_env("BOOL_VAR_YES", "yes");
    ASSERT_TRUE(sb_->get_env_bool("BOOL_VAR_YES"));
    sb_->set_env("BOOL_VAR_ZERO", "0");
    ASSERT_FALSE(sb_->get_env_bool("BOOL_VAR_ZERO"));
    sb_->set_env("BOOL_VAR_POSITIVE_INT", "5");
    ASSERT_TRUE(sb_->get_env_bool("BOOL_VAR_POSITIVE_INT"));
}

TEST_F(SwitchboardTest, GetEnvCharReturnsNullptrWhenUnset) {
    ASSERT_EQ(sb_->get_env_char("SOME_VAR_NOT_SET_XYZ"), nullptr);
}

TEST_F(SwitchboardTest, GetEnvCharReturnsTheValueWhenSet) {
    sb_->set_env("CHAR_VAR", "test_value");
    const char* val = sb_->get_env_char("CHAR_VAR");
    ASSERT_NE(val, nullptr);
    ASSERT_STREQ(val, "test_value");
}

TEST_F(SwitchboardTest, GetEnvIntParsesIntegers) {
    sb_->set_env("INT_VAR", "42");
    ASSERT_EQ(sb_->get_env_int("INT_VAR"), 42);
}

TEST_F(SwitchboardTest, GetEnvIntReturnsDefaultOnParseFailure) {
    sb_->set_env("NOT_AN_INT", "not_a_number");
    ASSERT_EQ(sb_->get_env_int("NOT_AN_INT", 7), 7);
}

TEST_F(SwitchboardTest, GetEnvLongUlongAndDoubleParseCorrectly) {
    sb_->set_env("LONG_VAR", "123456789");
    ASSERT_EQ(sb_->get_env_long("LONG_VAR"), 123456789L);

    sb_->set_env("ULONG_VAR", "4000000000");
    ASSERT_EQ(sb_->get_env_ulong("ULONG_VAR"), 4000000000UL);

    sb_->set_env("DOUBLE_VAR", "3.14");
    ASSERT_NEAR(sb_->get_env_double("DOUBLE_VAR"), 3.14, 1e-9);
}

TEST_F(SwitchboardTest, EnvNamesIncludesThePrePopulatedDefaultVars) {
    auto names = sb_->env_names();
    ASSERT_NE(std::find(names.begin(), names.end(), "ILLIXR_LOG_LEVEL"), names.end());
}

// --- topic registration, writer/reader round-trip ----------------------------------------------

TEST_F(SwitchboardTest, TopicExistsReflectsWhetherATopicHasBeenRegistered) {
    ASSERT_FALSE(sb_->topic_exists("not_registered_yet"));
    sb_->get_writer<int_type>("now_registered");
    ASSERT_TRUE(sb_->topic_exists("now_registered"));
}

TEST_F(SwitchboardTest, GetTopicThrowsForAnUnregisteredTopic) {
    ASSERT_THROW(sb_->get_topic("does_not_exist"), std::runtime_error);
}

TEST_F(SwitchboardTest, WriterPutThenReaderGetRoRoundTrips) {
    auto writer = sb_->get_writer<switchboard::event_wrapper<int>>("topic1");
    auto reader = sb_->get_reader<switchboard::event_wrapper<int>>("topic1");

    ASSERT_EQ(reader.get_ro_nullable(), nullptr); // nothing published yet

    writer.put(writer.allocate<switchboard::event_wrapper<int>>(switchboard::event_wrapper<int>(42)));

    ASSERT_EQ(**reader.get_ro(), 42);
}

TEST_F(SwitchboardTest, GetRoThrowsWhenNothingHasBeenPublishedYet) {
    auto reader = sb_->get_reader<switchboard::event_wrapper<int>>("empty_topic");
    ASSERT_THROW(reader.get_ro(), std::runtime_error);
}

TEST_F(SwitchboardTest, GetRwReturnsAnIndependentMutableCopy) {
    auto writer = sb_->get_writer<switchboard::event_wrapper<int>>("topic_rw");
    auto reader = sb_->get_reader<switchboard::event_wrapper<int>>("topic_rw");

    writer.put(writer.allocate<switchboard::event_wrapper<int>>(switchboard::event_wrapper<int>(10)));
    auto rw_copy = reader.get_rw();
    **rw_copy    = 99;

    ASSERT_EQ(**reader.get_ro(), 10); // the topic's own stored value is unaffected
    ASSERT_EQ(**rw_copy, 99);
}

TEST_F(SwitchboardTest, LatestValueIsTheMostRecentlyPublishedOne) {
    auto writer = sb_->get_writer<switchboard::event_wrapper<int>>("topic_seq");
    auto reader = sb_->get_reader<switchboard::event_wrapper<int>>("topic_seq");

    writer.put(writer.allocate<switchboard::event_wrapper<int>>(switchboard::event_wrapper<int>(1)));
    writer.put(writer.allocate<switchboard::event_wrapper<int>>(switchboard::event_wrapper<int>(2)));
    writer.put(writer.allocate<switchboard::event_wrapper<int>>(switchboard::event_wrapper<int>(3)));

    ASSERT_EQ(**reader.get_ro(), 3);
}

TEST_F(SwitchboardTest, MismatchedTypeOnAnExistingTopicAborts) {
#ifndef NDEBUG
    sb_->get_writer<switchboard::event_wrapper<int>>("typed_topic");
    ASSERT_DEATH({ sb_->get_reader<switchboard::event_wrapper<double>>("typed_topic"); }, "");
#else
    GTEST_SKIP() << "Topic type mismatch is only enforced via assert()/abort() in debug builds.";
#endif
}

// --- buffered_reader ----------------------------------------------------------------------------

TEST_F(SwitchboardTest, BufferedReaderCollectsEveryPublishedEvent) {
    auto writer  = sb_->get_writer<switchboard::event_wrapper<int>>("buffered_topic");
    auto breader = sb_->get_buffered_reader<switchboard::event_wrapper<int>>("buffered_topic");

    writer.put(writer.allocate<switchboard::event_wrapper<int>>(switchboard::event_wrapper<int>(1)));
    writer.put(writer.allocate<switchboard::event_wrapper<int>>(switchboard::event_wrapper<int>(2)));

    ASSERT_TRUE(wait_until([&breader]() { return breader.size() == 2; }, std::chrono::milliseconds{500}));

    ASSERT_EQ(**breader.dequeue(), 1);
    ASSERT_EQ(**breader.dequeue(), 2);
}

TEST_F(SwitchboardTest, BufferedReaderTryDequeueReturnsNullWhenEmpty) {
    auto breader = sb_->get_buffered_reader<switchboard::event_wrapper<int>>("empty_buffered_topic");
    ASSERT_EQ(breader.try_dequeue(), nullptr);
}

// --- schedule() / synchronous callbacks ---------------------------------------------------------

TEST_F(SwitchboardTest, ScheduleInvokesCallbackOnEveryPublishedEvent) {
    std::atomic<int> callback_count{0};
    std::atomic<int> last_value{0};

    sb_->schedule<switchboard::event_wrapper<int>>(
        /*plugin_id=*/0, "scheduled_topic",
                      [&](switchboard::ptr<const switchboard::event_wrapper<int>> event, std::size_t) {
                        callback_count++;
                        last_value = **event;
                      });

    auto writer = sb_->get_writer<switchboard::event_wrapper<int>>("scheduled_topic");
    writer.put(writer.allocate<switchboard::event_wrapper<int>>(switchboard::event_wrapper<int>(55)));

    ASSERT_TRUE(wait_until([&callback_count]() { return callback_count.load() > 0; }, std::chrono::milliseconds{500}));
    ASSERT_EQ(last_value.load(), 55);
}

TEST_F(SwitchboardTest, StopPreventsFurtherCallbacksFromRunning) {
    std::atomic<int> callback_count{0};

    sb_->schedule<switchboard::event_wrapper<int>>(0, "stoppable_topic",
                                                   [&](switchboard::ptr<const switchboard::event_wrapper<int>>, std::size_t) {
                                                     callback_count++;
                                                   });

    auto writer = sb_->get_writer<switchboard::event_wrapper<int>>("stoppable_topic");
    writer.put(writer.allocate<switchboard::event_wrapper<int>>(switchboard::event_wrapper<int>(1)));
    ASSERT_TRUE(wait_until([&callback_count]() { return callback_count.load() > 0; }, std::chrono::milliseconds{500}));

    sb_->stop();

    int count_at_stop = callback_count.load();
    writer.put(writer.allocate<switchboard::event_wrapper<int>>(switchboard::event_wrapper<int>(2)));
    std::this_thread::sleep_for(std::chrono::milliseconds{50});
    ASSERT_EQ(callback_count.load(), count_at_stop);
}

// --- coordinate_system / root_coordinates -------------------------------------------------------

TEST_F(SwitchboardTest, RootCoordinatesDefaultToOriginAndIdentityRotation) {
    const auto& pos = sb_->root_coordinates.position();
    const auto& ori = sb_->root_coordinates.orientation();
    EXPECT_FLOAT_EQ(pos.x(), 0.0f);
    EXPECT_FLOAT_EQ(pos.y(), 0.0f);
    EXPECT_FLOAT_EQ(pos.z(), 0.0f);
    EXPECT_FLOAT_EQ(ori.w(), 1.0f);
    EXPECT_FLOAT_EQ(ori.x(), 0.0f);
    EXPECT_FLOAT_EQ(ori.y(), 0.0f);
    EXPECT_FLOAT_EQ(ori.z(), 0.0f);
}

// These construct their own switchboard after setting WCS_ORIGIN, since the fixture's SetUp()
// would otherwise construct sb_ before the env var is in place.
TEST(SwitchboardWcsOriginTest, ParsesThreeComponentPositionFromEnv) {
    scoped_env_var env{"WCS_ORIGIN", "1.5,2.5,3.5"};
    switchboard    sb{nullptr};
    EXPECT_FLOAT_EQ(sb.root_coordinates.position().x(), 1.5f);
    EXPECT_FLOAT_EQ(sb.root_coordinates.position().y(), 2.5f);
    EXPECT_FLOAT_EQ(sb.root_coordinates.position().z(), 3.5f);
}

TEST(SwitchboardWcsOriginTest, ParsesFourComponentOrientationFromEnv) {
    scoped_env_var env{"WCS_ORIGIN", "0.7,0.1,0.2,0.3"};
    switchboard    sb{nullptr};
    EXPECT_FLOAT_EQ(sb.root_coordinates.orientation().w(), 0.7f);
    EXPECT_FLOAT_EQ(sb.root_coordinates.orientation().x(), 0.1f);
    EXPECT_FLOAT_EQ(sb.root_coordinates.orientation().y(), 0.2f);
    EXPECT_FLOAT_EQ(sb.root_coordinates.orientation().z(), 0.3f);
}

TEST(SwitchboardWcsOriginTest, ParsesSevenComponentPositionAndOrientationFromEnv) {
    scoped_env_var env{"WCS_ORIGIN", "1,2,3,0.5,0.1,0.2,0.3"};
    switchboard    sb{nullptr};
    EXPECT_FLOAT_EQ(sb.root_coordinates.position().x(), 1.0f);
    EXPECT_FLOAT_EQ(sb.root_coordinates.position().y(), 2.0f);
    EXPECT_FLOAT_EQ(sb.root_coordinates.position().z(), 3.0f);
    EXPECT_FLOAT_EQ(sb.root_coordinates.orientation().w(), 0.5f);
    EXPECT_FLOAT_EQ(sb.root_coordinates.orientation().x(), 0.1f);
    EXPECT_FLOAT_EQ(sb.root_coordinates.orientation().y(), 0.2f);
    EXPECT_FLOAT_EQ(sb.root_coordinates.orientation().z(), 0.3f);
}

class SwitchboardNetworkTest : public ::testing::Test {
protected:
    void SetUp() override {
        pb_.register_impl<record_logger>(std::make_shared<mock_record_logger>());
        backend_ = std::make_shared<mock_tcp_backend>();
        pb_.register_impl<network::tcp_backend>(backend_);
        sb_ = std::make_unique<switchboard>(&pb_);
    }

    phonebook                         pb_;
    std::shared_ptr<mock_tcp_backend> backend_;
    std::unique_ptr<switchboard>      sb_;
};

TEST_F(SwitchboardNetworkTest, GetNetworkWriterCallsTopicCreateOnce) {
    auto writer = sb_->get_network_writer<switchboard::event_wrapper<int>>("net_topic");
    ASSERT_EQ(backend_->created_topics_.size(), 1u);
    EXPECT_EQ(backend_->created_topics_[0], "net_topic");

    // A second writer for the same topic should not re-create it.
    auto writer2 = sb_->get_network_writer<switchboard::event_wrapper<int>>("net_topic");
    ASSERT_EQ(backend_->created_topics_.size(), 1u);
}

TEST_F(SwitchboardNetworkTest, PutOnAnUnnetworkedTopicUsesTheLocalPathNotTheBackend) {
    auto writer = sb_->get_network_writer<switchboard::event_wrapper<int>>("local_net_topic");
    auto reader = sb_->get_reader<switchboard::event_wrapper<int>>("local_net_topic");

    // is_topic_networked() returns false by default here -- nothing has been marked networked.
    writer.put(writer.allocate<switchboard::event_wrapper<int>>(switchboard::event_wrapper<int>(7)));

    EXPECT_EQ(backend_->send_count_, 0);
    ASSERT_EQ(**reader.get_ro(), 7); // went through normal topic storage instead of the backend
}

TEST_F(SwitchboardNetworkTest, PutOnANetworkedTopicSendsThroughTheBackendInstead) {
    auto writer = sb_->get_network_writer<int_type>("wire_topic");
    auto reader = sb_->get_reader<int_type>("wire_topic");
    backend_->mark_networked("wire_topic");

    writer.put(writer.allocate<int_type>(int_type(9)));

    EXPECT_EQ(backend_->send_count_, 1);
    EXPECT_EQ(backend_->last_sent_topic_, "wire_topic");
    EXPECT_EQ(reader.get_ro_nullable(), nullptr); // the networked path bypasses local storage entirely
}

TEST_F(SwitchboardNetworkTest, ThrowsWhenTheRequestedBackendIsNotRegistered) {
    // No udp_backend registered in this fixture's phonebook at all -- see the note above about
    // which exception actually gets thrown for this case versus what the docstring describes.
    network::topic_config udp_config{network::topic_config::BOOST, network::topic_config::UDP};
    ASSERT_THROW(sb_->get_network_writer<switchboard::event_wrapper<int>>("udp_topic", udp_config), std::exception);
}

} // namespace

} // namespace ILLIXRsss
