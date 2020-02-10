#include "concurrent_utils.hh"
#include <utility>

template <typename T1, typename T2>
using pair = std::pair<T1, T2>;

// T&& uses move-smenatics
// to 'move' instead of copy the data

template <typename In, typename Out>
class producer_consumer {
public:
	virtual Out produce_consume(In) = 0;
};

template <typename Event>
class producer {
public:
	virtual Event produce() = 0;

	template <typename ProducerConsumer>
	std::unique_ptr<producer<typename std::result_of<decltype(&ProducerConsumer::produce_consume)(Event)>::type>>
	map(ProducerConsumer proc) {
		return {
			bound_producer_consumer<ProducerConsumer, typename std::result_of<decltype(&ProducerConsumer::produce_consume)(Event)>::type> {
				proc,
			},
		};
	}

	template <typename Producer2>
	std::unique_ptr<producer<pair<Event, typename std::result_of<decltype(&Producer2::produce())()>::type>>>
	pair_with(Producer2 producer2) {
		return {
			pairer<Producer2, typename std::result_of<decltype(&Producer2::produce())()>::type> {
				producer2,
			},
		};
	}

private:

	template <typename ProducerConsumer, typename Out>
	class bound_producer_consumer : public producer<Out> {
	public:
		virtual Out produce() {
			return _m_proc->produce_consume(produce());
		}
		bound_producer_consumer(std::unique_ptr<producer<Event>> upstream, ProducerConsumer proc)
			: _m_proc{proc}
		{}
	private:
		ProducerConsumer _m_proc;
	};

	template <typename Producer2, typename Event2>
	class pairer : public producer<pair<Event, Event2>> {
	public:
		pairer(Producer2 producer2)
			: _m_producer2{producer2}
		{}

		virtual pair<Event, Event2> produce() {
			return {produce(), _m_producer2->produce()};
		}

	private:
		Producer2 _m_producer2;
	};
};

/*
template <typename T>
class async_producer : public producer<const T&>, public concurrent_loop {
public:
	async_producer(std::unique_ptr<producer<T>> producer)
		: _m_producer{producer}
	{ }
	virtual void main_loop() {
		// inside work loop
		_m_buffer.set(_m_producer->produce());
	}
	virtual const T& produce() {
		// outside of work loop
		return _m_buffer.get_complete();
	}
private:
	double_buffer<T> _m_buffer;
	std::unique_ptr<producer<T>> _m_producer;
};
*/
