#pragma once

#include <atomic>
#include <unordered_map>
#include <vector>
#include "phonebook.hpp"

namespace ILLIXR {

class record {
public:
	record(
		   std::size_t       component_id_,
		   std::size_t    subcomponent_id_ = 0,
		   std::size_t subsubcomponent_id_ = 0
	)   :       component_id{      component_id_}
	    ,    subcomponent_id{   subcomponent_id_}
	    , subsubcomponent_id{subsubcomponent_id_}
	    , time{std::chrono::high_resolution_clock::now()}
	{ }
private:
	[[maybe_unused]] std::size_t component_id;
	[[maybe_unused]] std::size_t subcomponent_id = 0;
	[[maybe_unused]] std::size_t subsubcomponent_id = 0;
 	[[maybe_unused]] std::chrono::time_point<std::chrono::high_resolution_clock> time;
};

#define RECORD_CLASS3(name, t0, r0, t1, r1, t2, r2)                     \
    class name : record {                                               \
    private:                                                            \
    [[maybe_unused]] t0 r0;                                                              \
    [[maybe_unused]] t1 r1;                                                              \
    [[maybe_unused]] t2 r2;                                                              \
    public:                                                             \
    name(t0 r0_, t1 r1_, t2 r2_, std::size_t component_id, std::size_t subcomponent_id = 0, std::size_t subsubcomponent_id = 0) \
        : record{component_id, subcomponent_id, subsubcomponent_id}     \
        , r0{r0_}                                                       \
        , r1{r1_}                                                       \
        , r2{r2_}                                                       \
        { }                                                             \
    };

#define RECORD_CLASS2(name, t0, r0, t1, r1)                             \
    class name : record {                                               \
    private:                                                            \
    [[maybe_unused]] t0 r0;                                                              \
    [[maybe_unused]] t1 r1;                                                              \
    public:                                                             \
    name(t0 r0_, t1 r1_, std::size_t component_id, std::size_t subcomponent_id = 0, std::size_t subsubcomponent_id = 0) \
        : record{component_id, subcomponent_id, subsubcomponent_id}     \
        , r0{r0_}                                                       \
        , r1{r1_}                                                       \
        { }                                                             \
    };

#define RECORD_CLASS1(name, t0, r0)                                     \
    class name : record {                                               \
    private:                                                            \
    [[maybe_unused]] t0 r0;                                                              \
    public:                                                             \
    name(t0 r0_, std::size_t component_id, std::size_t subcomponent_id = 0, std::size_t subsubcomponent_id = 0) \
        : record{component_id, subcomponent_id, subsubcomponent_id}     \
        , r0{r0_}                                                       \
        { }                                                             \
    };

#define RECORD_CLASS0(name)                                             \
    class name : record {                                               \
    public:                                                             \
    name(std::size_t component_id, std::size_t subcomponent_id = 0, std::size_t subsubcomponent_id = 0) \
        : record{component_id, subcomponent_id, subsubcomponent_id}     \
        { }                                                             \
    };

RECORD_CLASS1(misc_record, std::string, notes)
RECORD_CLASS1(component_start_record, std::string, name)
RECORD_CLASS0(component_stop_record)
RECORD_CLASS2(start_skip_record, std::size_t, iteration, std::size_t, skip_iteration)
RECORD_CLASS3(stop_skip_record, std::size_t, iteration, std::size_t, skip_iteration, std::size_t, result)
RECORD_CLASS1(start_iteration_record, std::size_t, iteration)
RECORD_CLASS1(stop_iteration_record, std::size_t, iteration)
RECORD_CLASS2(switchboard_communication_record, std::size_t, serial, std::size_t, bytes)

class c_logger : public phonebook::service {
public:
	template <typename Record>
	void log([[maybe_unused]] Record r) { }

	template <typename Record>
	void log([[maybe_unused]] std::unique_ptr<std::vector<Record>> r) { }
};

class c_gen_guid : public phonebook::service {
public:
	std::size_t get(std::size_t namespace_ = 0, std::size_t subnamespace = 0, std::size_t subsubnamespace = 0) {
		return ++guid_starts[namespace_][subnamespace][subsubnamespace];
	}
private:
	std::unordered_map<std::size_t, std::unordered_map<std::size_t, std::unordered_map<std::size_t, std::atomic<std::size_t>>>> guid_starts;
};

}
