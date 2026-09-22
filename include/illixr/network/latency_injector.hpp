#pragma once

#include <random>

namespace ILLIXR::network {
class latency_injector {
public:
    virtual long long get_latency() { return 0;};
    virtual ~latency_injector() = default;
};

class [[maybe_unused]] latency_none : public latency_injector {
public:
    long long get_latency() override {
        return 0;
    }
};

class [[maybe_unused]] latency_constant : public latency_injector {
public:
    [[maybe_unused]]explicit latency_constant(float value) : latency_{static_cast<long long>((std::max)(0.f, value))} { }

    long long get_latency() override {
        return latency_;
    }
private:
    const long long latency_;
};

class [[maybe_unused]] latency_random : public latency_injector {
public:
    [[maybe_unused]]explicit latency_random(float min_latency, float max_latency)
        : gen_{std::random_device{}()}
        , dist_{min_latency, max_latency} { }

    long long get_latency() override {
        return static_cast<long long>(dist_(gen_));
    };

private:
    std::mt19937 gen_;
    std::uniform_real_distribution<float> dist_;
};
}
