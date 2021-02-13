#pragma once

#include <memory>
#include <list>
#include <string>
#include <array>
#include <sstream>
#include <atomic>
#include <shared_mutex>
#include <type_traits>
#include <functional>
#include <chrono>
#include <exception>
#include "phonebook.hpp"
#include "cpu_timer.hpp"
#include "record_logger.hpp"
#include "managed_thread.hpp"
#include "../runtime/concurrentqueue/blockingconcurrentqueue.hpp"

namespace ILLIXR {

using plugin_id_t = std::size_t;

/**
 * @Should be private to Switchboard.
 */
const record_header __switchboard_callback_header {"switchboard_callback", {
    {"plugin_id", typeid(plugin_id_t)},
    {"topic_name", typeid(std::string)},
    {"iteration_no", typeid(std::size_t)},
    {"cpu_time_start", typeid(std::chrono::nanoseconds)},
    {"cpu_time_stop" , typeid(std::chrono::nanoseconds)},
    {"wall_time_start", typeid(std::chrono::high_resolution_clock::time_point)},
    {"wall_time_stop" , typeid(std::chrono::high_resolution_clock::time_point)},
}};

/**
 * @Should be private to Switchboard.
 */
const record_header __switchboard_topic_stop_header {"switchboard_topic_stop", {
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
 * switchboard::writer<topic2_type> topic2 = switchboard.get_reader<topic2_type>("topic1");
 *
 * while (true) {
 *     // Read topic 1
 *     switchboard::ptr<topic1_type> event1 = topic1.get();
 *
 *     // Write to topic 2
 *     topic2_type* event2 = topic2.allocate();
 *     // Populate the event
 *     event2->foo = 3;
 *     topic2.put(event2);
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
class switchboard : public phonebook::service {
public:

    /**
     * @brief The type of shared pointer returned by switchboard.
     *
     * TODO: Make this agnostic to the type of `ptr`
     * Currently, it depends on `ptr` == shared_ptr
     */
    template <typename specific_event>
    using ptr = std::shared_ptr<specific_event>;

    class event {
    public:
        virtual ~event() { }
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
    template <typename underlying_type>
    class event_wrapper : public event {
    private:
        underlying_type underlying_data;
    public:
        event_wrapper() { }

        event_wrapper(underlying_type underlying_data_)
            : underlying_data{underlying_data_}
        { }
        operator underlying_type() const { return underlying_data; }
        underlying_type& operator*() { return underlying_data; }
        const underlying_type& operator*() const { return underlying_data; }
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
    private:
        const std::string& _m_topic_name;
        plugin_id_t _m_plugin_id;
        std::function<void(ptr<const event>&&, std::size_t)> _m_callback;
        const std::shared_ptr<record_logger> _m_record_logger;
        record_coalescer _m_cb_log;
        moodycamel::BlockingConcurrentQueue<ptr<const event>> _m_queue {8 /*max size estimate*/};
        moodycamel::ConsumerToken _m_ctok {_m_queue};
        static constexpr std::chrono::milliseconds _m_queue_timeout {100};
        std::size_t _m_enqueued {0};
        std::size_t _m_dequeued {0};
        std::size_t _m_idle_cycles {0};

        // This needs to be last,
        // so it is destructed before the data it uses.
        managed_thread _m_thread;

        void thread_on_start() {
#ifndef NDEBUG
            // std::cerr << "Thread " << std::this_thread::get_id() << " start" << std::endl;
#endif
        }

        void thread_body() {
            // Try to pull event off of queue
            ptr<const event> this_event;
            std::int64_t timeout_usecs = std::chrono::duration_cast<std::chrono::microseconds>(_m_queue_timeout).count();
            // Note the use of timed blocking wait
            if (_m_queue.wait_dequeue_timed(_m_ctok, this_event, timeout_usecs)) {
                // Process event
                // Also, record and log the time
                _m_dequeued++;
                auto cb_start_cpu_time  = thread_cpu_time();
                auto cb_start_wall_time = std::chrono::high_resolution_clock::now();
                // std::cerr << "deq " << ptr_to_str(reinterpret_cast<const void*>(this_event.get())) << " " << this_event.use_count() << " v\n";
                _m_callback(std::move(this_event), _m_dequeued);
                if (_m_cb_log) {
                    _m_cb_log.log(record{__switchboard_callback_header, {
                        {_m_plugin_id},
                        {_m_topic_name},
                        {_m_dequeued},
                        {cb_start_cpu_time},
                        {thread_cpu_time()},
                        {cb_start_wall_time},
                        {std::chrono::high_resolution_clock::now()},
                    }});
                }
            } else {
                // Nothing to do.
                _m_idle_cycles++;
            }
        }


        void thread_on_stop() {
            // Drain queue
            std::size_t unprocessed = _m_enqueued - _m_dequeued;
            {
                ptr<const event> this_event;
                for (std::size_t i = 0; i < unprocessed; ++i) {
                    [[maybe_unused]] bool ret = _m_queue.try_dequeue(_m_ctok, this_event);
                    assert(ret);
                    // std::cerr << "deq (stopping) " << ptr_to_str(reinterpret_cast<const void*>(this_event.get())) << " " << this_event.use_count() << " v\n";
                    this_event.reset();
                }
            }

            // Log stats
            if (_m_record_logger) {
                _m_record_logger->log(record{__switchboard_topic_stop_header, {
                    {_m_topic_name},
                    {_m_dequeued},
                    {unprocessed},
                    {_m_idle_cycles},
                }});
            }
        }

    public:
        topic_subscription(const std::string& topic_name, plugin_id_t plugin_id, std::function<void(ptr<const event>&&, std::size_t)> callback, std::shared_ptr<record_logger> record_logger_)
            : _m_topic_name{topic_name}
            , _m_plugin_id{plugin_id}
            , _m_callback{callback}
            , _m_record_logger{record_logger_}
            , _m_cb_log{record_logger_}
            , _m_thread{[this]{this->thread_body();}, [this]{this->thread_on_start();}, [this]{this->thread_on_stop();}}
        {
            _m_thread.start();
        }

        /**
         * @brief Tells the subscriber about @p this_event
         *
         * Thread-safe
         */
        void enqueue(ptr<const event>&& this_event) {
            if (_m_thread.get_state() == managed_thread::state::running) {
                [[maybe_unused]] bool ret = _m_queue.enqueue(std::move(this_event));
                assert(ret);
                _m_enqueued++;
            }
        }
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
     * However, this class can have _dynamic_ type-information in _m_ty, that gets set and checked
     * at runtime.
     */
    class topic {
    private:
        const std::string _m_name;
        const std::type_info& _m_ty;
        const std::shared_ptr<record_logger> _m_record_logger;
		std::atomic<size_t> _m_latest_index;
		static constexpr std::size_t _m_latest_buffer_size = 256;
		std::array<ptr<const event>, _m_latest_buffer_size> _m_latest_buffer;
        std::list<topic_subscription> _m_subscriptions;
        std::shared_mutex _m_subscriptions_lock;

    public:
        topic(
            std::string name,
            const std::type_info& ty,
            std::shared_ptr<record_logger> record_logger_
        )   : _m_name{name}
            , _m_ty{ty}
            , _m_record_logger{record_logger_}
			, _m_latest_index{0}
        { }

        const std::string& name() { return _m_name; }

        const std::type_info& ty() { return _m_ty; }

        /**
         * @brief Gets a read-only copy of the most recent event on the topic.
         */
        ptr<const event> get() const {
			size_t idx = _m_latest_index.load() % _m_latest_buffer_size;
			ptr<const event> this_event = _m_latest_buffer[idx];
			// if (this_event) {
			// 	std::cerr << "get " << ptr_to_str(reinterpret_cast<const void*>(this_event.get())) << " " << this_event.use_count() << "v \n";
			// }
			return this_event;
        }

        /**
         * @brief Publishes @p this_event to the topic
         *
         * Thread-safe
         */
        void put(ptr<const event>&& this_event) {
			assert(this_event != nullptr);
			// assert(this_event.unique());  /// <-- TODO: Revisit for solution that guarantees uniqueness

			/* The pointer that this gets exchanged with needs to get dropped. */
			size_t index = (_m_latest_index.load() + 1) % _m_latest_buffer_size;
			_m_latest_buffer[index] = this_event;
			_m_latest_index++;

            // Read/write on _m_subscriptions.
            // Must acquire shared state on _m_subscriptions_lock
            std::unique_lock lock{_m_subscriptions_lock};
            for (topic_subscription& ts : _m_subscriptions) {
                // std::cerr << "enq " << ptr_to_str(reinterpret_cast<const void*>(this_event->get())) << " " << this_event->use_count() << " ^\n";
                ptr<const event> event_ptr_copy {this_event};
                ts.enqueue(std::move(event_ptr_copy));
            }
            // std::cerr << "put done " << ptr_to_str(reinterpret_cast<const void*>(this_event->get())) << " " << this_event->use_count() << " (= 1 + len(sub)) \n";
        }

        /**
         * @brief Schedules @p callback on the topic (@p plugin_id is for accounting)
         *
         * Thread-safe
         */
        void schedule(
            plugin_id_t plugin_id,
            std::function<void(ptr<const event>&&, std::size_t)> callback)
        {
            // Write on _m_subscriptions.
            // Must acquire unique state on _m_subscriptions_lock
            const std::unique_lock lock{_m_subscriptions_lock};
            _m_subscriptions.emplace_back(_m_name, plugin_id, callback, _m_record_logger);
        }

        /**
         * @brief Stop and remove all topic_subscription threads.
         *
         * Thread-safe
         */
        void stop() {
            // Write on _m_subscriptions.
            // Must acquire unique state on _m_subscriptions_lock
            const std::unique_lock lock{_m_subscriptions_lock};
            _m_subscriptions.clear();
        }
    };

public:

    /**
     * @brief A handle which can read the latest event on a topic.
     */
    template <typename specific_event>
    class reader {
    private:
        /// Reference to the underlying topic
        topic& _m_topic;

    public:
        reader(topic& topic_)
            : _m_topic{topic_}
        {
#ifndef NDEBUG
            if (typeid(specific_event) != _m_topic.ty()) {
                std::cerr << "topic '" << _m_topic.name() << "' holds type " << _m_topic.ty().name()
                          << ", but caller used type" << typeid(specific_event).name() << std::endl;
                abort();
            }
#endif
        }

       /**
        * @brief Gets a "read-only" copy of the latest value.
        *
        * This will return null if no event is on the topic yet.
        */
       ptr<const specific_event> get_ro_nullable() const noexcept {
          ptr<const event> this_event = _m_topic.get();
          ptr<const specific_event> this_specific_event = std::dynamic_pointer_cast<const specific_event>(this_event);

           if (this_event != nullptr) {
			   assert(this_specific_event /* Otherwise, dynamic cast failed; dynamic type information could be wrong*/);
               return this_specific_event;
           } else {
               return ptr<const specific_event>{nullptr};
           }
       }

       /**
        * @brief Gets a non-null "read-only" copy of the latest value.
        *
        * @throws `runtime_error` If no event is on the topic yet.
        */
        ptr<const specific_event> get_ro() const {
           ptr<const specific_event> this_specific_event = get_ro_nullable();
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
        ptr<specific_event> get_rw() const {
           /*
             This method is currently not more efficient than calling get_ro() and making a copy,
             but in the future it could be.
            */
           ptr<const specific_event> this_specific_event = get();
           return std::make_shared<specific_event>(*this_specific_event);
        }

        /// Member function alias for common case `get_ro`
        const std::function< ptr<const specific_event>() > get = [this]() { return get_ro(); };
    };

    /**
     * @brief A handle which can publish events to a topic.
     */
    template <typename specific_event>
    class writer {
    private:
        // Reference to the underlying topic
        topic& _m_topic;

    public:
        writer(topic& topic_)
            : _m_topic{topic_}
        { }

        /**
         * @brief Publish @p ev to this topic.
         */
         void put(const specific_event* this_specific_event) {
			assert(typeid(specific_event) == _m_topic.ty());
			assert(this_specific_event);
			ptr<const event> this_event {static_cast<const event*>(this_specific_event)};
			assert(this_event.unique());
			_m_topic.put(std::move(this_event));
        }

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
        ptr<specific_event> allocate(Args&&... args) {
			return std::make_shared<specific_event>(std::forward<Args>(args)...);
        }

        /**
         * @brief Publish @p ev to this topic.
         */
		void put(ptr<specific_event>&& this_specific_event) {
			assert(typeid(specific_event) == _m_topic.ty());
			assert(this_specific_event != nullptr);
			assert(this_specific_event.unique());
			ptr<const event> this_event = std::const_pointer_cast<const event>(std::static_pointer_cast<event>(std::move(this_specific_event)));
			assert(this_event.unique());
			_m_topic.put(std::move(this_event));
        }
    };

private:
    std::unordered_map<std::string, topic> _m_registry;
    std::shared_mutex _m_registry_lock;
    std::shared_ptr<record_logger> _m_record_logger;

    template <typename specific_event>
    topic& try_register_topic(const std::string& topic_name) {
        {
            const std::shared_lock lock{_m_registry_lock};
            auto found = _m_registry.find(topic_name);
            if (found != _m_registry.end()) {
                topic& topic_ = found->second;
#ifndef NDEBUG
                if (typeid(specific_event) != topic_.ty()) {
                    std::cerr << "topic '" << topic_name << "' holds type " << topic_.ty().name()
                              << ", but caller used type" << typeid(specific_event).name()
                              << std::endl;
                    abort();
                }
#endif
                return topic_;
            }
        }

#ifndef NDEBUG
        std::cerr << "Creating: " << topic_name << " for " << typeid(specific_event).name() << std::endl;
#endif
        // Topic not found. Need to create it here.
        const std::unique_lock lock{_m_registry_lock};
        return _m_registry.try_emplace(topic_name, topic_name, typeid(specific_event), _m_record_logger).first->second;

    }

public:

    /**
     * If @p pb is null, then logging is disabled.
     */
    switchboard(const phonebook* pb)
        : _m_record_logger{pb ? pb->lookup_impl<record_logger>() : nullptr}
    { }

    /**
     * @brief Schedules the callback @p fn every time an event is published to @p topic_name.
     *
     * Switchboard maintains a threadpool to call @p fn.
     *
     * This is safe to be called from any thread.
     *
     * @throws if topic already exists and its type does not match the @p event.
     */
    template <typename specific_event>
    void schedule(plugin_id_t plugin_id, std::string topic_name, std::function<void(ptr<const specific_event>&&, std::size_t)> fn) {
        try_register_topic<specific_event>(topic_name).schedule(plugin_id, [=](ptr<const event>&& this_event, std::size_t it_no) {
            assert(this_event);
            ptr<const specific_event> this_specific_event = std::dynamic_pointer_cast<const specific_event>(std::move(this_event));
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
    template <typename specific_event>
    writer<specific_event> get_writer(const std::string& topic_name) {
        return writer<specific_event>{try_register_topic<specific_event>(topic_name)};
    }

    /**
     * @brief Gets a handle to read to the latest value from the topic @p topic_name.
     *
     * This is safe to be called from any thread.
     *
     * @throws If topic already exists, and its type does not match the @p event.
     */
    template <typename specific_event>
    reader<specific_event> get_reader(const std::string& topic_name) {
        return reader<specific_event>{try_register_topic<specific_event>(topic_name)};
    }

    /**
     * @brief Stops calling switchboard callbacks.
     *
     * This is safe to be called from any thread.
     *
     * Leave topics in place, so existing reader/writer handles will not crash.
     */
    void stop() {
        const std::shared_lock lock{_m_registry_lock};
        for (auto& pair : _m_registry) {
            pair.second.stop();
        }
    }
};

}
