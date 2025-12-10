#pragma once

#if defined(_WIN32) || defined(_WIN64)
    #include <cstdlib>
#endif
#include "concurrentqueue/blockingconcurrentqueue.hpp"
#include "export.hpp"
#include "managed_thread.hpp"
#include "network/network_backend.hpp"
#include "network/topic_config.hpp"
#include "phonebook.hpp"
#include "record_logger.hpp"

#ifdef Success
    #undef Success // For 'Success' conflict
#endif

#include <eigen3/Eigen/Core>
#include <eigen3/Eigen/Geometry>
#include <iostream>
#include <list>
#include <mutex>
#include <shared_mutex>
#include <utility>

#ifndef NDEBUG
    #include <spdlog/spdlog.h>
#endif

#if __has_include("cpu_timer.hpp")
    #include "cpu_timer.hpp"
#else
static std::chrono::nanoseconds thread_cpu_time() {
    return {};
}
#endif

#include <boost/archive/binary_iarchive.hpp>
#include <boost/archive/binary_oarchive.hpp>
#include <boost/iostreams/device/back_inserter.hpp>
#include <boost/iostreams/stream.hpp>
#include <boost/serialization/shared_ptr.hpp>

namespace ILLIXR {

using plugin_id_t = std::size_t;

const std::vector<std::string> ignore_vars = {"plugins"};
const std::vector<std::string> ENV_VARS    = {
    "ILLIXR_ENABLE_PRE_SLEEP",
    "ILLIXR_LOG_LEVEL",
    "ILLIXR_RUN_DURATION",
};
/**
 * @Should be private to Switchboard.
 */
const record_header _switchboard_callback_header{
    "switchboard_callback",
    {
        {"plugin_id", typeid(plugin_id_t)},
        {"topic_name", typeid(std::string)},
        {"iteration_no", typeid(std::size_t)},
        {"cpu_time_start", typeid(std::chrono::nanoseconds)},
        {"cpu_time_stop", typeid(std::chrono::nanoseconds)},
        {"wall_time_start", typeid(std::chrono::high_resolution_clock::time_point)},
        {"wall_time_stop", typeid(std::chrono::high_resolution_clock::time_point)},
    }};

/**
 * @Should be private to Switchboard.
 */
const record_header _switchboard_topic_stop_header{"switchboard_topic_stop",
                                                   {
                                                       {"plugin_id", typeid(plugin_id_t)},
                                                       {"topic_name", typeid(std::string)},
                                                       {"enqueued", typeid(std::size_t)},
                                                       {"dequeued", typeid(std::size_t)},
                                                       {"idle_cycles", typeid(std::size_t)},
                                                   }};

/**
 * @brief A manager for typesafe, threadsafe, named event-streams (called
 * topics).
 *
 * - Writing: One can write to a topic (in any thread) through the
 * object returned by `get_writer()`.
 *
 * - There are two ways of reading: asynchronous reading and synchronous
 * reading:
 *
 *   - Asynchronous reading returns the most-recent event on the topic
 * (idempotently). One can do this through (in any thread) the
 * object returned by `get_reader()`.
 *
 *   - Synchronous reading schedules a callback to be executed on _every_ event
 * which gets published. One can schedule computation by `schedule()`, which
 * will run the computation in a thread managed by switchboard.
 *
 * \code{.cpp}
 * // Get a reader on topic1
 * switchboard::reader<topic1_type> topic1 = switchboard.get_reader<topic1_type>("topic1");
 *
 * // Get a writer on topic2
 * switchboard::writer<topic2_type> topic2 = switchboard.get_writer<topic2_type>("topic2");
 *
 * while (true) {
 *     // Read topic 1
 *     switchboard::ptr<topic1_type> event1 = topic1.get_rw();
 *
 *     // Write to topic 2 using topic 1 input
 *     topic2.put(topic2.allocate<topic2_type>( do_something(event1->foo) ));
 * }
 *
 * // Read topic 3 synchronously
 * switchboard.schedule<topic3_type>(plugin_id, "topic3", [&](switchboard::ptr<topic3_type> event3, std::size_t it) {
 *     // This is a lambda expression
 *     // https://en.cppreference.com/w/cpp/language/lambda
 *     std::cout << "Got a new event on topic3: " << event3->foo << " for iteration " << it << std::endl;
 * });
 * \endcode
 */
class MY_EXPORT_API switchboard : public phonebook::service {
public:
    /**
     * @brief The type of shared pointer returned by switchboard.
     *
     * TODO: Make this agnostic to the type of `ptr`
     * Currently, it depends on `ptr` == shared_ptr
     */
    template<typename Specific_event>
    using ptr = std::shared_ptr<Specific_event>;

    /**
     * @brief Virtual class for event types.
     *
     * Minimum requirement: Events must be destructible.
     * Casting events from various sources to void* (aka type-punning) breaks [strict-aliasing][1]
     * and is undefined behavior in modern C++.
     * Therefore, we require a common supertype for all events.
     * We will cast them to this common supertype, event* instead.

     * [1] https://cellperformance.beyond3d.com/articles/2006/06/understanding-strict-aliasing.html
     */
    class event {
    public:
        template<typename Archive>
        [[maybe_unused]] void serialize(Archive& ar, const unsigned int version) {
            (void) ar;
            (void) version;
        }

        virtual ~event() = default;
    };

    /**
     * @brief Helper class for making event types
     *
     * Since topic has no static type-information on the contained events, this class does not
     * either.
     *
     * \code{.cpp}
     * event_wrapper<int> int_event = 5;
     * \endcode
     */
    template<typename Underlying_type>
    class event_wrapper : public event {
    public:
        event_wrapper() = default;

        explicit event_wrapper(Underlying_type underlying_data)
            : underlying_data_{std::move(underlying_data)} { }

        explicit operator Underlying_type() const {
            return underlying_data_;
        }

        Underlying_type& operator*() {
            return underlying_data_;
        }

        const Underlying_type& operator*() const {
            return underlying_data_;
        }

    private:
        Underlying_type underlying_data_;
    };

private:
    /**
     * @brief Represents a single topic_subscription (callback and queue)
     *
     * This class treats everything as `event`s (type-erased) because `topic` treats everything as
     * `event`s.
     *
     * Each topic can have 0 or more topic_subscriptions.
     */
    class topic_subscription {
    public:
        topic_subscription(const std::string& topic_name, plugin_id_t plugin_id,
                           std::function<void(ptr<const event>&&, std::size_t)> callback,
                           const std::shared_ptr<record_logger>&                record_logger_)
            : topic_name_{topic_name}
            , plugin_id_{plugin_id}
            , callback_{std::move(callback)}
            , record_logger_{record_logger_}
            , cb_log_{record_logger_}
            , thread_{[this] {
                          this->thread_body();
                      },
                      [] {
                          thread_on_start();
                      },
                      [this] {
                          this->thread_on_stop();
                      }} {
            thread_.start();
        }

        /**
         * @brief Tells the subscriber about @p this_event
         *
         * Thread-safe
         */
        void enqueue(ptr<const event>&& this_event) {
            if (thread_.get_state() == managed_thread::state::running) {
                [[maybe_unused]] bool ret = queue_.enqueue(std::move(this_event));
                assert(ret);
                enqueued_++;
            }
        }

    private:
        static void thread_on_start() {
#ifndef NDEBUG
            // spdlog::get("illixr")->set_pattern("[%Y-%m-%d %H:%M:%S.%e] [%n] [%^%l%$] [switchboard] thread %t %v");
            // spdlog::get("illixr")->debug("start");
            // spdlog::get("illixr")->set_pattern("%+");
#endif
        }

        void thread_body() {
            // Try to pull event off of queue
            ptr<const event> this_event;
            std::int64_t     timeout_usecs = std::chrono::duration_cast<std::chrono::microseconds>(queue_timeout_).count();
            // Note the use of timed blocking wait
            if (queue_.wait_dequeue_timed(token_, this_event, timeout_usecs)) {
                // Process event
                // Also, record and log the time
                dequeued_++;
                auto cb_start_cpu_time  = thread_cpu_time();
                auto cb_start_wall_time = std::chrono::high_resolution_clock::now();
                // std::cerr << "deq " << ptr_to_str(reinterpret_cast<const void*>(this_event.get_ro())) << " " <<
                // this_event.use_count() << " v\n";
                callback_(std::move(this_event), dequeued_);
                if (cb_log_) {
                    cb_log_.log(record{_switchboard_callback_header,
                                       {
                                           {plugin_id_},
                                           {topic_name_},
                                           {dequeued_},
                                           {cb_start_cpu_time},
                                           {thread_cpu_time()},
                                           {cb_start_wall_time},
                                           {std::chrono::high_resolution_clock::now()},
                                       }});
                }
            } else {
                // Nothing to do.
                idle_cycles_++;
            }
        }

        void thread_on_stop() {
            // Drain queue
            std::size_t unprocessed = enqueued_ - dequeued_;
            {
                ptr<const event> this_event;
                for (std::size_t i = 0; i < unprocessed; ++i) {
                    [[maybe_unused]] bool ret = queue_.try_dequeue(token_, this_event);
                    assert(ret);
                    // std::cerr << "deq (stopping) " << ptr_to_str(reinterpret_cast<const void*>(this_event.get_ro())) << " "
                    // << this_event.use_count() << " v\n";
                    this_event.reset();
                }
            }

            // Log stats
            if (record_logger_) {
                record_logger_->log(record{_switchboard_topic_stop_header,
                                           {
                                               {plugin_id_},
                                               {topic_name_},
                                               {dequeued_},
                                               {unprocessed},
                                               {idle_cycles_},
                                           }});
            }
        }

        const std::string&                                    topic_name_;
        plugin_id_t                                           plugin_id_;
        std::function<void(ptr<const event>&&, std::size_t)>  callback_;
        const std::shared_ptr<record_logger>                  record_logger_;
        record_coalescer                                      cb_log_;
        moodycamel::BlockingConcurrentQueue<ptr<const event>> queue_{8 /*max size estimate*/};
        moodycamel::ConsumerToken                             token_{queue_};
        static constexpr std::chrono::milliseconds            queue_timeout_{100};
        std::size_t                                           enqueued_{0};
        std::size_t                                           dequeued_{0};
        std::size_t                                           idle_cycles_{0};

        // This needs to be last,
        // so it is destructed before the data it uses.
        managed_thread thread_;
    };

    class topic_buffer {
    public:
        topic_buffer() {
#ifndef NDEBUG
            spdlog::get("illixr")->info("[switchboard] topic buffer created");
#endif
        }

        void enqueue(ptr<const event>&& this_event) {
            queue_size_++;
            [[maybe_unused]] bool ret = queue_.enqueue(std::move(this_event));
            assert(ret);
        }

        [[nodiscard]] size_t size() const {
            return queue_size_;
        }

        ptr<const event> dequeue() {
            ptr<const event> obj;
            queue_size_--;
            queue_.wait_dequeue(token_, obj);
            return obj;
        }

    private:
        moodycamel::BlockingConcurrentQueue<ptr<const event>> queue_{8 /*max size estimate*/};
        moodycamel::ConsumerToken                             token_{queue_};
        std::atomic<size_t>                                   queue_size_{0};
    };

    /**
     * @brief Represents a topic
     *
     * Note, this class cannot have any static type-information about the contained events because I
     * need to hold all of the topics in a homogeneous container (can't add topic<int> and
     * topic<float> to the same std::vector<topic<WHAT_GOES_HERE>>).
     *
     * Therefore, this class uses type-erasure, and regards all events as type `event`. I could have
     * used `std::any` for this, but I think inheriting `event` will be slightly more efficient
     * because it avoids a heap-allocation.
     *
     * However, this class can have _dynamic_ type-information in ty, that gets set and checked
     * at runtime.
     */
    class topic {
    public:
        topic(std::string name, const std::type_info& ty, std::shared_ptr<record_logger> record_logger_)
            : name_{std::move(name)}
            , type_info_{ty}
            , record_logger_{std::move(record_logger_)}
            , latest_index_{0} { }

        const std::string& name() {
            return name_;
        }

        const std::type_info& ty() {
            return type_info_;
        }

        /**
         * @brief Gets a read-only copy of the most recent event on the topic.
         */
        [[nodiscard]] ptr<const event> get() const {
            size_t           idx        = latest_index_.load() % latest_buffer_size_;
            ptr<const event> this_event = latest_buffer_[idx];
            // if (this_event) {
            // 	std::cerr << "get " << ptr_to_str(reinterpret_cast<const void*>(this_event.get())) << " " <<
            // this_event.use_count() << "v \n";
            // }
            return this_event;
        }

        /**
         * @brief Publishes @p this_event to the topic
         *
         * Thread-safe
         * - Caveat:
         *
         *   This (circular) queue based solution may race if >= N write attempts
         *       to the N-sized queue interrupt a concurrent reader (using 'get').
         *
         *   The reader's critical section is as follows:
         *   1. Read the latest serial number
         *   2. Compute the serial's modulus
         *   3. Dereference and access the position in the queue/array
         *
         *   The critical section is extremely small, so a race is unlikely, albeit possible.
         *   The probability of a data race decreases geometrically with N.
         */
        void put(ptr<const event>&& this_event) {
            assert(this_event != nullptr);
            assert(this_event.unique() ||
                   this_event.use_count() <= 2); /// <-- TODO: Revisit for solution that guarantees uniqueness

            /* The pointer that this gets exchanged with needs to get dropped. */
            size_t index          = (latest_index_.load() + 1) % latest_buffer_size_;
            latest_buffer_[index] = this_event;
            latest_index_++;

            // Read/write on subscriptions_.
            // Must acquire shared state on subscriptions_lock_
            std::unique_lock lock{subscriptions_lock_};
            for (topic_subscription& ts : subscriptions_) {
                // std::cerr << "enq " << ptr_to_str(reinterpret_cast<const void*>(this_event->get())) << " " <<
                // this_event->use_count() << " ^\n";
                ptr<const event> event_ptr_copy{this_event};
                ts.enqueue(std::move(event_ptr_copy));
            }

            for (topic_buffer& ts : buffers_) {
                // std::cerr << "enq " << ptr_to_str(reinterpret_cast<const void*>(this_event->get())) << " " <<
                // this_event->use_count() << " ^\n";
                ptr<const event> event_ptr_copy{this_event};
                ts.enqueue(std::move(event_ptr_copy));
            }
            // std::cerr << "put done " << ptr_to_str(reinterpret_cast<const void*>(this_event->get())) << " " <<
            // this_event->use_count() << " (= 1 + len(sub)) \n";
        }

        [[maybe_unused]] void deserialize_and_put(std::vector<char>& buffer, network::topic_config& config) {
            if (config.serialization_method == network::topic_config::SerializationMethod::BOOST) {
                // TODO: Need to differentiate and support protobuf deserialization
                boost::iostreams::stream<boost::iostreams::array_source> stream{buffer.data(), buffer.size()};
                boost::archive::binary_iarchive                          ia{stream};
                ptr<event>                                               this_event;
                ia >> this_event;
                put(std::move(this_event));
            } else {
                ptr<event> message = std::make_shared<event_wrapper<std::string>>((std::string(buffer.begin(), buffer.end())));
                put(std::move(message));
            }
        }

        /**
         * @brief Schedules @p callback on the topic (@p plugin_id is for accounting)
         *
         * Thread-safe
         */
        void schedule(plugin_id_t plugin_id, const std::function<void(ptr<const event>&&, std::size_t)>& callback) {
            // Write on subscriptions_.
            // Must acquire unique state on subscriptions_lock_
            const std::unique_lock lock{subscriptions_lock_};
            subscriptions_.emplace_back(name_, plugin_id, callback, record_logger_);
        }

        topic_buffer& get_buffer() {
            const std::unique_lock lock{subscriptions_lock_};
            buffers_.emplace_back();
            return buffers_.back();
        }

        /**
         * @brief Stop and remove all topic_subscription threads.
         *
         * Thread-safe
         */
        void stop() {
            // Write on subscriptions_.
            // Must acquire unique state on subscriptions_lock_
            const std::unique_lock lock{subscriptions_lock_};
            subscriptions_.clear();
        }

    private:
        static constexpr std::size_t latest_buffer_size_ = 256;

        const std::string                                 name_;
        const std::type_info&                             type_info_;
        const std::shared_ptr<record_logger>              record_logger_;
        std::atomic<size_t>                               latest_index_;
        std::array<ptr<const event>, latest_buffer_size_> latest_buffer_;
        std::list<topic_subscription>                     subscriptions_;
        std::list<topic_buffer>                           buffers_;
        std::shared_mutex                                 subscriptions_lock_;
    };

public:
    /**
     * @brief A handle which can read the latest event on a topic.
     */
    template<typename Specific_event>
    class reader {
    public:
        explicit reader(topic& topic)
            : topic_{topic} {
#ifndef NDEBUG
            if (typeid(Specific_event) != topic_.ty()) {
                spdlog::get("illixr")->error("[switchboard] topic '{}' holds type {}, but caller used type {}", topic_.name(),
                                             topic_.ty().name(), typeid(Specific_event).name());
                abort();
            }
#endif
        }

        /**
         * @brief Gets a "read-only" copy of the latest value.
         *
         * This will return null if no event is on the topic yet.
         */
        ptr<const Specific_event> get_ro_nullable() const noexcept {
            ptr<const event>          this_event          = topic_.get();
            ptr<const Specific_event> this_specific_event = std::dynamic_pointer_cast<const Specific_event>(this_event);

            if (this_event != nullptr) {
                assert(this_specific_event /* Otherwise, dynamic cast failed; dynamic type information could be wrong*/);
                return this_specific_event;
            } else {
                return ptr<const Specific_event>{nullptr};
            }
        }

        /**
         * @brief Gets a non-null "read-only" copy of the latest value.
         *
         * @throws `runtime_error` If no event is on the topic yet.
         */
        ptr<const Specific_event> get_ro() const {
            ptr<const Specific_event> this_specific_event = get_ro_nullable();
            if (this_specific_event != nullptr) {
                return this_specific_event;
            } else {
                /// Otherwise, no event on the topic yet
                throw std::runtime_error("No event on topic");
            }
        }

        /**
         * @brief Gets a non-null mutable copy of the latest value.
         *
         * @throws `runtime_error` If no event is on the topic yet.
         */
        [[maybe_unused]] ptr<Specific_event> get_rw() const {
            /*
              This method is currently not more efficient than calling get_ro() and making a copy,
              but in the future it could be.
             */
            ptr<const Specific_event> this_specific_event = get_ro();
            return std::make_shared<Specific_event>(*this_specific_event);
        }

    private:
        /// Reference to the underlying topic
        topic& topic_;
    };

    template<typename Specific_event>
    class buffered_reader {
    public:
        explicit buffered_reader(topic& topic)
            : topic_{topic}
            , topic_buffer_{topic_.get_buffer()} { }

        [[nodiscard]] size_t size() const {
            return topic_buffer_.size();
        }

        virtual ptr<const Specific_event> dequeue() {
            // CPU_TIMER_TIME_EVENT_INFO(true, false, "callback", cpu_timer::make_type_eraser<FrameInfo>("", topic_.name(),
            // serial_no_));
            serial_no_++;
            ptr<const event>          this_event          = topic_buffer_.dequeue();
            ptr<const Specific_event> this_specific_event = std::dynamic_pointer_cast<const Specific_event>(this_event);
            return this_specific_event;
        }

    private:
        topic&        topic_;
        size_t        serial_no_ = 0;
        topic_buffer& topic_buffer_;
    };

    /**
     * @brief A handle which can publish events to a topic.
     */
    template<typename Specific_event>
    class writer {
    public:
        explicit writer(topic& topic)
            : topic_{topic} { }

        /**
         * @brief Like `new`/`malloc` but more efficient for this specific case.
         *
         * There is an optimization available which has not yet been implemented: switchboard can reuse memory
         * from old events, like a [slab allocator][1]. Suppose module A publishes data for module
         * B. B's deallocation through the destructor, and A's allocation through this method completes
         * the cycle in a [double-buffer (AKA swap-chain)][2].
         *
         * [1]: https://en.wikipedia.org/wiki/Slab_allocation
         * [2]: https://en.wikipedia.org/wiki/Multiple_buffering
         */
        template<class... Args>
        ptr<Specific_event> allocate(Args&&... args) {
            return std::make_shared<Specific_event>(std::forward<Args>(args)...);
        }

        /**
         * @brief Publish @p ev to this topic.
         */
        virtual void put(ptr<Specific_event>&& this_specific_event) {
            assert(typeid(Specific_event) == topic_.ty());
            assert(this_specific_event != nullptr);
            assert(this_specific_event.unique());
            ptr<const event> this_event =
                std::const_pointer_cast<const event>(std::static_pointer_cast<event>(std::move(this_specific_event)));
            assert(this_event.unique() ||
                   this_event.use_count() <= 2); /// TODO: Revisit for solution that guarantees uniqueness
            topic_.put(std::move(this_event));
        }

    protected:
        // Reference to the underlying topic
        topic& topic_;
    };

    template<typename Serializable_event>
    class network_writer : public writer<Serializable_event> {
    public:
        explicit network_writer(topic& topic, ptr<network::network_backend> backend = nullptr,
                                const network::topic_config& config = {})
            : writer<Serializable_event>{topic}
            , backend_{std::move(backend)}
            , config_{config} { }

        void put(ptr<Serializable_event>&& this_specific_event) override {
            if (backend_->is_topic_networked(this->topic_.name())) {
                if (config_.serialization_method == network::topic_config::SerializationMethod::BOOST) {
                    auto base_event = std::dynamic_pointer_cast<event>(std::move(this_specific_event));
                    assert(base_event && "Event is not derived from switchboard::event");
                    // Default serialization method - Boost
                    std::vector<char>                                                                        buffer;
                    boost::iostreams::back_insert_device<std::vector<char>>                                  inserter{buffer};
                    boost::iostreams::stream_buffer<boost::iostreams::back_insert_device<std::vector<char>>> stream{inserter};
                    boost::archive::binary_oarchive                                                          oa{stream};
                    oa << base_event;
                    // flush
                    stream.pubsync();
                    backend_->topic_send(this->topic_.name(), std::move(std::string(buffer.begin(), buffer.end())));
                } else {
                    // PROTOBUF - this_specific_event will be a string
                    auto        message_ptr = std::dynamic_pointer_cast<event_wrapper<std::string>>(this_specific_event);
                    std::string message     = **message_ptr;
                    backend_->topic_send(this->topic_.name(), std::move(message));
                }
            } else {
                writer<Serializable_event>::put(std::move(this_specific_event));
            }
        }

    private:
        ptr<network::network_backend> backend_;
        network::topic_config         config_;
    };

    template<typename Serializable_event>
    class local_writer : public writer<Serializable_event> {
    public:
        explicit local_writer(topic& topic, ptr<network::local_network_backend> backend = nullptr,
                              const network::topic_config& config = {})
            : writer<Serializable_event>{topic}
            , backend_{std::move(backend)}
            , config_{config} { }

        void put(ptr<Serializable_event>&& this_specific_event) override {
            if (backend_->is_topic_networked(this->topic_.name())) {
                if (config_.serialization_method == network::topic_config::SerializationMethod::BOOST) {
                    auto base_event = std::dynamic_pointer_cast<event>(std::move(this_specific_event));
                    assert(base_event && "Event is not derived from switchboard::event");
                    // Default serialization method - Boost
                    std::vector<char>                                                                        buffer;
                    boost::iostreams::back_insert_device<std::vector<char>>                                  inserter{buffer};
                    boost::iostreams::stream_buffer<boost::iostreams::back_insert_device<std::vector<char>>> stream{inserter};
                    boost::archive::binary_oarchive                                                          oa{stream};
                    oa << base_event;
                    // flush
                    stream.pubsync();
                    backend_->topic_send(this->topic_.name(), std::move(std::string(buffer.begin(), buffer.end())));
                } else {
                    // PROTOBUF - this_specific_event will be a string
                    auto        message_ptr = std::dynamic_pointer_cast<event_wrapper<std::string>>(this_specific_event);
                    std::string message     = **message_ptr;
                    backend_->topic_send(this->topic_.name(), std::move(message));
                }
            } else {
                writer<Serializable_event>::put(std::move(this_specific_event));
            }
        }

    private:
        ptr<network::local_network_backend> backend_;
        network::topic_config               config_;
    };

public:
    /**
     * If @p pb is null, then logging is disabled.
     */
    explicit switchboard(const phonebook* pb)
        : phonebook_{pb}
        , record_logger_{pb ? pb->lookup_impl<record_logger>() : nullptr} {
        for (const auto& item : ENV_VARS) {
            char* value = getenv(item.c_str());
            if (value) {
                env_vars_[item] = value;
            } else {
                env_vars_[item] = "";
            }
        }
    }

    [[maybe_unused]] bool topic_exists(const std::string& topic_name) {
        const std::shared_lock lock{registry_lock_};
        auto                   found = registry_.find(topic_name);
        return found != registry_.end();
    }

    [[maybe_unused]] topic& get_topic(const std::string& topic_name) {
        const std::shared_lock lock{registry_lock_};
        auto                   found = registry_.find(topic_name);
        if (found != registry_.end()) {
            return found->second;
        } else {
            throw std::runtime_error("Topic not found");
        }
    }

    /**
     * @brief Set the local environment variable to the given value
     */
    void set_env(const std::string& var, const std::string& val) {
        env_vars_[var] = val;
        setenv(var.c_str(), val.c_str(), 1);
    }

    /**
     * @brief Get a vector of the currently known environment variables
     */
    std::vector<std::string> env_names() const {
        std::vector<std::string> keys(env_vars_.size());
        std::transform(env_vars_.begin(), env_vars_.end(), keys.begin(), [](auto pair) {
            return pair.first;
        });
        return keys;
    }

    /**
     * @brief Switchboard access point for environment variables
     *
     * If the given variable `var` has a non-empty entry in the map, that value is returned. If the
     * entry is empty then the system getenv is called. If this is non-empty then that value is stored
     * and returned, otherwise the default value is returned (not stored).
     */
    std::string get_env(const std::string& var, std::string _default = "") {
        try {
            if (!env_vars_.at(var).empty())
                return env_vars_.at(var);
            env_vars_.at(var) = _default;
            return _default;
        } catch (std::out_of_range&) {
            char* val = std::getenv(var.c_str());
            if (val) {
                std::string temp(val);
                set_env(var, val); // store it locally for faster retrieval
                return temp;
            }
            return _default;
        }
    }

    /**
     * @brief Get the boolean value of the given environment variable
     */
    bool get_env_bool(const std::string& var, const std::string& def = "false") {
        std::string val = get_env(var, def);
        // see if we are dealing with an int value
        try {
            const int i_val = std::stoi(val);
            if (i_val <= 0)
                return false;
            return true;
        } catch (...) { }

        const std::vector<std::string> affirmative{"yes", "y", "true", "on"};
        for (auto s : affirmative) {
            if (std::equal(val.begin(), val.end(), s.begin(), s.end(), [](char a, char b) {
                    return std::tolower(a) == std::tolower(b);
                }))
                return true;
        }
        return false;
    }

    /**
     * @brief Get a char* of the given environment variable
     */
    const char* get_env_char(const std::string& var, const std::string _default = "") {
        std::string val = get_env(var, _default);
        if (val.empty())
            return nullptr;
        return strdup(val.c_str());
    }

    [[maybe_unused]] long get_env_long(const std::string& var, const long _default = 0) {
        std::string val = get_env(var, "");
        if (val.empty())
            return _default;
        return std::stol(val);
    }

    [[maybe_unused]] unsigned long get_env_ulong(const std::string& var, const unsigned long _default = 0) {
        std::string val = get_env(var, "");
        if (val.empty())
            return _default;
        return std::stoul(val);
    }

    [[maybe_unused]] double get_env_double(const std::string& var, const double _default = 0.) {
        std::string val = get_env(var, "");
        if (val.empty())
            return _default;
        return std::stod(val);
    }

    /**
     * @brief Schedules the callback @p fn every time an event is published to @p topic_name.
     *
     * Switchboard maintains a threadpool to call @p fn.
     *
     * This is safe to be called from any thread.
     *
     * @throws if topic already exists and its type does not match the @p event.
     */
    template<typename Specific_event>
    void schedule(plugin_id_t plugin_id, std::string topic_name,
                  std::function<void(ptr<const Specific_event>&&, std::size_t)> fn) {
        try_register_topic<Specific_event>(topic_name)
            .schedule(plugin_id, [=](ptr<const event>&& this_event, std::size_t it_no) {
                assert(this_event);
                ptr<const Specific_event> this_specific_event =
                    std::dynamic_pointer_cast<const Specific_event>(std::move(this_event));
                assert(this_specific_event);
                fn(std::move(this_specific_event), it_no);
            });
    }

    /**
     * @brief Gets a handle to publish to the topic @p topic_name.
     *
     * This is safe to be called from any thread.
     *
     * @throws If topic already exists, and its type does not match the @p event.
     */
    template<typename Specific_event>
    writer<Specific_event> get_writer(const std::string& topic_name) {
        return writer<Specific_event>{try_register_topic<Specific_event>(topic_name)};
    }

    template<typename Specific_event>
    network_writer<Specific_event> get_network_writer(const std::string& topic_name, network::topic_config config = {}) {
        auto backend = phonebook_->lookup_impl<network::network_backend>();
        if (registry_.find(topic_name) == registry_.end()) {
            backend->topic_create(topic_name, config);
        }
        return network_writer<Specific_event>{try_register_topic<Specific_event>(topic_name), backend, config};
    }

    template<typename Specific_event>
    local_writer<Specific_event> get_local_network_writer(const std::string& topic_name, network::topic_config config = {}) {
        auto backend = phonebook_->lookup_impl<network::local_network_backend>();
        if (registry_.find(topic_name) == registry_.end()) {
            backend->topic_create(topic_name, config);
        }
        return local_writer<Specific_event>{try_register_topic<Specific_event>(topic_name), backend, config};
    }

    /**
     * @brief Gets a handle to read to the latest value from the topic @p topic_name.
     *
     * This is safe to be called from any thread.
     *
     * @throws If topic already exists, and its type does not match the @p event.
     */
    template<typename Specific_event>
    reader<Specific_event> get_reader(const std::string& topic_name) {
        return reader<Specific_event>{try_register_topic<Specific_event>(topic_name)};
    }

    template<typename Specific_event>
    buffered_reader<Specific_event> get_buffered_reader(const std::string& topic_name) {
        return buffered_reader<Specific_event>{try_register_topic<Specific_event>(topic_name)};
    }

    /**
     * @brief Stops calling switchboard callbacks.
     *
     * This is safe to be called from any thread.
     *
     * Leave topics in place, so existing reader/writer handles will not crash.
     */
    void stop() {
        const std::shared_lock lock{registry_lock_};
        for (auto& pair : registry_) {
            pair.second.stop();
        }
    }

private:
    const phonebook*                             phonebook_;
    std::unordered_map<std::string, topic>       registry_;
    std::shared_mutex                            registry_lock_;
    std::shared_ptr<record_logger>               record_logger_;
    std::unordered_map<std::string, std::string> env_vars_;

    template<typename Specific_event>
    topic& try_register_topic(const std::string& topic_name) {
        {
            const std::shared_lock lock{registry_lock_};
            auto                   found = registry_.find(topic_name);
            if (found != registry_.end()) {
                topic& _topic = found->second;
#ifndef NDEBUG
                if (typeid(Specific_event) != _topic.ty()) {
                    spdlog::get("illixr")->error("[switchboard] topic '{}' holds type {}, but caller used type {}", topic_name,
                                                 _topic.ty().name(), typeid(Specific_event).name());
                    abort();
                }
#endif
                return _topic;
            }
        }

#ifndef NDEBUG
        spdlog::get("illixr")->debug("[switchboard] Creating: {} for {}", topic_name, typeid(Specific_event).name());
#endif
        // Topic not found. Need to create it here.
        const std::unique_lock lock{registry_lock_};
        return registry_.try_emplace(topic_name, topic_name, typeid(Specific_event), record_logger_).first->second;
    }

    /**
     * @brief Base coordinate system
     *
     * This class reads in and hold the world coordinate system origin. The origin can be provided by the
     * WCS_ORIGIN environment/yaml variable and can be specified in one of three ways
     *
     *    - a set of 3 comma separated values, representing only the origin in x, y, and z coordinates
     *    - a set of 4 comma separated values, representing only the quaternion of the origin in w, x, y, z
     *    - a set of 7 comma seperated values, representing both the origin and its quaternion in the form x, y, z, w, wx, wy,
     * wz
     *
     * Any component which is not given defaults to 0 (except w which is set to 1)
     */
    class coordinate_system {
    private:
        Eigen::Vector3f    position_;
        Eigen::Quaternionf orientation_;

    public:
        coordinate_system()
            : position_{0., 0., 0.}
            , orientation_{1., 0., 0., 0.} {
            const char* ini_pose = getenv("WCS_ORIGIN");
            // =
            // if (!ini_pose.empty()) {
            if (ini_pose) {
                std::string        ini_pose_str(ini_pose);
                std::stringstream  iss(ini_pose_str);
                std::string        token;
                std::vector<float> ip;
                while (!iss.eof() && std::getline(iss, token, ',')) {
                    ip.emplace_back(std::stof(token));
                }
                if (ip.size() == 3) {
                    position_.x() = ip[0];
                    position_.y() = ip[1];
                    position_.z() = ip[2];
                } else if (ip.size() == 4) {
                    orientation_.w() = ip[0];
                    orientation_.x() = ip[1];
                    orientation_.y() = ip[2];
                    orientation_.z() = ip[3];
                } else if (ip.size() == 7) {
                    position_.x()    = ip[0];
                    position_.y()    = ip[1];
                    position_.z()    = ip[2];
                    orientation_.w() = ip[3];
                    orientation_.x() = ip[4];
                    orientation_.y() = ip[5];
                    orientation_.z() = ip[6];
                }
            }
        }

        /**
         * Get the position portion of the WCS origin
         * @return Eigen::Vector3f
         */
        [[nodiscard]] const Eigen::Vector3f& position() const {
            return position_;
        }

        /**
         * Get the orientation portion of the WCS origin
         * @return Eigen::Quaternionf
         */
        [[nodiscard]] const Eigen::Quaternionf& orientation() const {
            return orientation_;
        }
    };

public:
    coordinate_system root_coordinates; //!> The WCS origin
};

} // namespace ILLIXR
