#include "common/switchboard.hh"
#include "common/data_format.hh"
#include <unordered_map>
#include <atomic>
#include <vector>
#include <iostream>
#include <cassert>

#include <unistd.h>
#include <signal.h>

namespace ILLIXR {

	class topic {
	public:

		class topic_reader_latest : public reader_latest<void> {
		public:
			virtual const void* get_latest_ro() const override {
				return _m_topic->_m_latest.load();
			}

			virtual void* get_latest() const override {
				/* TODO: (feature) support this method. The immediate
				   problem is that the type has been erased at this
				   point, so I don't know how to copy a void*. */
				throw std::runtime_error{"this doesn't work yet"};
				/* TODO: (optimization) avoid this copy if we are the
				   only subscriber. The reader can read and modify the
				   event in place. */
			}
			topic_reader_latest(const topic* topic) : _m_topic{topic} { }
			virtual ~topic_reader_latest() { }

		private:
			const topic *const _m_topic;
		};

		class topic_writer : public writer<void> {
		public:
			virtual void* allocate() override {
				/* TODO: (feature:allocate) support this method. The
				   immediate problem is that the type has been erased
				   at this point, so I don't know how to copy a
				   void*. */
				throw std::runtime_error{"this doesn't work yet"};
				/* Note on memory safety: allocated pointers always
				   get moved into put(). Then they are always freed
				   when put() is called again or when the destructor
				   is called. */
				/* TODO: (optimization:free-list) pop from free list if non-empty. */

			}

			virtual void put(const void* contents) override {
				const void* old __attribute__((unused)) =
					_m_topic->_m_latest.exchange(contents);

				// delete old;
				/* TODO: (feature:allocate) Free old.*/
				/* TODO: (optimization:free-list) return to free-list. */

				for (const auto& callback __attribute__((unused)) : _m_topic->_m_callbacks) {
					throw std::runtime_error{"this doesn't work yet"};
					/* TODO: (feature) I need to schedule these
					   callbacks to run on a thread-worker.*/
				}
			}
			topic_writer(topic* topic) : _m_topic{topic} { }
			virtual ~topic_writer() { }

		private:
			/* TODO: (optimization:free-list) maintain a free-list. allocate
			   should pop and return an object from the list. The
			   destructor should put it back on the list. This needs
			   to be done in the destructor, not in `put`, because
			   even though the reader_latest are not reading the
			   object, it could still be scheduled for synchronous
			   processing. */
			/* TODO: (optimization) pre-allocate in a static array. */
			topic * const _m_topic;
		};

		std::unique_ptr<topic_writer> get_writer() {
			return std::make_unique<topic_writer>(this);
		}

		std::unique_ptr<topic_reader_latest> get_reader_latest() const {
			return std::make_unique<topic_reader_latest>(this);
		}

		void schedule(std::function<void()> callback) {
			_m_callbacks.push_back(callback);
		}

		std::size_t ty() { return _m_ty; }

		topic(std::size_t ty)
			: _m_ty{ty}
		{ }

		~topic() {
			const void* latest = _m_latest.exchange(nullptr);
			if (latest) {
				/* TODO: (feature:allocate) Free old.*/
			}
			/* TODO: (optimization:free-list) free the elements of free-list. */
		}

	private:

		const std::size_t _m_ty;
		std::atomic<const void*> _m_latest {nullptr};
		std::vector<std::function<void()>> _m_callbacks;
		/* - const because nobody should write to the _m_latest in
		   place.
		   - atomic because it will be accessed from different threads. */
		/* TODO: (optimization) use unique_ptr when there is only one
		   subscriber. */
		/* TODO: (optimization) use relaxed memory_order? */

	};

	class switchboard_impl : public switchboard {

	public:

		virtual ~switchboard_impl() { }

	private:
		virtual void _p_schedule(const std::string& name, std::function<void()> callback, std::size_t ty) {
			_m_registry.try_emplace(name, ty);
			_m_registry.at(name).schedule(callback);
		}

		virtual std::unique_ptr<writer<void>> _p_publish(const std::string& name, std::size_t ty) {
			_m_registry.try_emplace(name, ty);
			topic& topic = _m_registry.at(name);
			assert(topic.ty() == ty);
			return std::unique_ptr<writer<void>>(topic.get_writer().release());
			/* TODO: (code beautify) why can't I write
			   return std::move(topic.get_writer());
			*/
		}

		virtual std::unique_ptr<reader_latest<void>> _p_subscribe_latest(const std::string& name, std::size_t ty) {
			_m_registry.try_emplace(name, ty);
			topic& topic = _m_registry.at(name);
			assert(topic.ty() == ty);
			return std::unique_ptr<reader_latest<void>>(topic.get_reader_latest().release());
			/* TODO: (code beautify) why can't I write
			   return std::move(topic.get_reader_latest());
			*/
		}

		std::unordered_map<std::string, topic> _m_registry;
	};

	std::unique_ptr<switchboard> create_switchboard() {
		// return std::move(std::unique_ptr<switchboard>{new switchboard_impl});

		// return std::move(std::unique_ptr<switchboard>{
		// 	static_cast<switchboard*>(std::make_unique<switchboard_impl>().release())
		// });

		// return std::move(std::unique_ptr<switchboard>{
		// 	static_cast<switchboard*>(new switchboard_impl)
		// });

		// return std::unique_ptr<switchboard>{
		// 	static_cast<switchboard*>(new switchboard_impl)
		// };

		return std::unique_ptr<switchboard>{new switchboard_impl};
	}
}
