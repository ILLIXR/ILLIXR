#include "../cpu_timer.hpp"

#include <gtest/gtest.h>
#include <opencv/cv.hpp>

namespace ILLIXR {

class ILLIXRCommon : public ::testing::Test { };

class Adder {
public:
    Adder(int j_)
        : j{j_} { }

    int j;

    void add(int& i, int k) {
        i += j + k;
    }
};

TEST_F(ILLIXRCommon, CPUTimer) {
    Adder adder{5};
    int   i = 4, k = 3;

    std::thread t = timed_thread("increment", &Adder::add, &adder, std::ref(i), k);
    t.join();

    ASSERT_EQ(i, 12);
}

} // namespace ILLIXR
