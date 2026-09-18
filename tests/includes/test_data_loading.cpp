/**
 * @brief Unit tests for load_data (include/illixr/data_loading.hpp).
 *
 * load_data() itself does no parsing -- it locates a file via $ILLIXR_DATA plus a subpath, opens
 * it, and hands the open stream to a caller-supplied parser function. So these tests focus on
 * that plumbing (finding the right file, handing off a valid, correctly-positioned stream, and
 * the two abort() paths) using a minimal two-column file the test creates and removes itself,
 * rather than on parsing any particular format.
 */

#include "illixr/data_loading.hpp"
#include "illixr/phonebook.hpp"
#include "illixr/record_logger.hpp"
#include "illixr/switchboard.hpp"

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <gtest/gtest.h>
#include <map>
#include <memory>
#include <string>
#include <unistd.h>

namespace ILLIXR {

namespace {

/**
 * @brief Saves and restores an environment variable's previous value around a test.
 */
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

/// switchboard's constructor looks up a record_logger from the phonebook unconditionally; this
/// stands in for one so that lookup succeeds. It's never actually exercised in these tests.
class mock_record_logger : public record_logger {
public:
    void log(const record& r) override {
        (void) r;
    }
};

int         g_parse_calls = 0;
std::string g_last_first_line;

/**
 * @brief A minimal stand-in parser: proves the stream it's handed is open and readable at the
 * right position, without needing to actually parse the two-column format.
 */
std::map<ullong, int> record_call_and_read_first_line(std::ifstream& stream, const std::string& path) {
    (void) path;
    g_parse_calls++;
    std::getline(stream, g_last_first_line);
    return std::map<ullong, int>{{0, 1}};
}

std::shared_ptr<switchboard> make_switchboard(phonebook& pb) {
    pb.register_impl<record_logger>(std::make_shared<mock_record_logger>());
    return std::make_shared<switchboard>(&pb);
}

class DataLoadingTest : public ::testing::Test {
protected:
    void SetUp() override {
        g_parse_calls = 0;
        g_last_first_line.clear();

        base_dir_ = std::filesystem::temp_directory_path() / ("illixr_test_data_loading_" + std::to_string(getpid()));
        sub_dir_  = base_dir_ / "test_plugin";
        std::filesystem::create_directories(sub_dir_);

        std::ofstream{sub_dir_ / "data.csv"} << "1,alpha\n2,beta\n";

        pb_ = std::make_unique<phonebook>();
        sb_ = make_switchboard(*pb_);
    }

    void TearDown() override {
        std::filesystem::remove_all(base_dir_);
    }

    std::filesystem::path        base_dir_;
    std::filesystem::path        sub_dir_;
    std::unique_ptr<phonebook>   pb_;
    std::shared_ptr<switchboard> sb_;
};

TEST_F(DataLoadingTest, LocatesOpensAndHandsOffTheFile) {
scoped_env_var env{"ILLIXR_DATA", base_dir_.string()};

std::map<ullong, int> result =
    load_data<int>("test_plugin", "test_plugin_name", record_call_and_read_first_line, sb_);

ASSERT_EQ(g_parse_calls, 1);
ASSERT_EQ(g_last_first_line, "1,alpha");
ASSERT_EQ(result.at(0), 1);
}

TEST_F(DataLoadingTest, UsesTheGivenFileNameInsteadOfTheDefault) {
std::ofstream{sub_dir_ / "custom.csv"} << "9,gamma\n";
scoped_env_var env{"ILLIXR_DATA", base_dir_.string()};

load_data<int>("test_plugin", "test_plugin_name", record_call_and_read_first_line, sb_, "custom.csv");

ASSERT_EQ(g_last_first_line, "9,gamma");
}

TEST(DataLoadingDeathTest, AbortsWhenIllixrDataIsUnset) {
auto attempt = []() {
  unsetenv("ILLIXR_DATA");
  phonebook pb;
  auto      sb = make_switchboard(pb);
  load_data<int>("anything", "test_plugin_name", record_call_and_read_first_line, sb);
};
#ifdef NDEBUG
EXPECT_EXIT(attempt(), ::testing::ExitedWithCode(1), "");
#else
EXPECT_EXIT(attempt(), ::testing::KilledBySignal(SIGABRT), "");
#endif
}

TEST(DataLoadingDeathTest, AbortsWhenTheFileDoesNotExist) {
auto attempt = []() {
  setenv("ILLIXR_DATA", "/tmp", 1);
  phonebook pb;
  auto      sb = make_switchboard(pb);
  load_data<int>("path/that/does/not/exist", "test_plugin_name", record_call_and_read_first_line, sb);
};
#ifdef NDEBUG
EXPECT_EXIT(attempt(), ::testing::ExitedWithCode(1), "");
#else
EXPECT_EXIT(attempt(), ::testing::KilledBySignal(SIGABRT), "");
#endif
}

} // namespace

} // namespace ILLIXR
