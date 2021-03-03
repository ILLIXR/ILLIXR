#pragma once
#include <utility>
#include <iostream>
#include <iterator>
#include <cassert>

/**
 * # Background
 *
 * C++ classes are like Russian dolls. Inheritence of classes is analogous to encapsulation of
 * dolls. The derived class contains inherits everything the base class has, plus a little more. The
 * dolls have to be put together inner before outer, since the outer contains the inner. They have
 * to be taken apart in the exact opposite order outer before inner. Likewise, the base class gets
 * constructed before the derived class and destructed after it. This is the only way it can be;
 * since the derived class could call the bass class, the derived class's lifetime must be a subset
 * of the base class's.
 *
 * \code{.cpp}
 * struct Base {
 *     Base() { std::cout << "1: Base constructor\n"; foo_vector = initial_value(); }
 *     ~Base(){ std::cout << "6: Base destructor \n"; }
 *     void foo() {
 *         // If we are getting here, we can assume the constructor already ran and initialized foo_vector.
 *         std::cout << "4: Base::foo()"
 *         foo_vector.push_back(34);
 *         // How would I even call Derived::bar from Base?
 *     }
 *     // Other code not shown
 * };
 * struct Derived : Base {
 *     Derived() { std::cout << "2: Derived constructor\n"; bar_vector = initial_value(); }
 *     ~Derived(){ std::cout << "5: Derived destructor \n"; }
 *     void baz() {
 *         std::cout << "3: Derived::baz"
 *         foo(); // calls base class method
 *     }
 *     void bar() {
 *         // If we are getting here, we can assume the constructor already ran and initialized bar_vector.
 *         bar_vector.push_back(45);
 *     }
 * };
 * 
 * Derived obj;
 * obj.baz();
 * \endcode
 *
 * Usually the derived class calls the base class (aka "upcalls"), but never the other way around
 * (aka "downcalls"). How would it? The astute reader will note that the `Derived` class can call
 * `bar` by making it `virtual`.
 *
 * \code{.cpp}
 * struct Base {
 *     Base() { std::cout << "1: Base constructor\n"; foo_vector = initial_value(); }
 *     ~Base(){ std::cout << "7: Base destructor \n"; }
 *     void foo() {
 *         // If we are getting here, we can assume the constructor already ran and initialized foo_vector.
 *         std::cout << "4: Base::foo()"
 *         foo_vector.push_back(34);
 *         bar();
 *     }
 *     virtual void bar() = 0;
 * };
 * struct Derived : Base {
 *     Derived() { std::cout << "2: Derived constructor\n"; bar_vector = initial_value(); }
 *     ~Derived(){ std::cout << "6: Derived destructor \n"; }
 *     void baz() {
 *         std::cout << "3: Derived::baz"
 *         foo(); // calls base class method
 *     }
 *     virtual void bar() override {
 *         // If we are getting here, we can assume the constructor already ran and initialized bar_vector.
 *         std::cout << "5: Derived::bar"
 *         bar_vector.push_back(45);
 *     }
 * };
 * 
 * Derived obj;
 * obj.baz();
 * \endcode
 *
 * But this poses a problem for the con/destructor order. What happens if the `Base` class tries to
 * call `bar` in its constructor? The `Derived` constructor has not yet run. If `bar` can't even try
 * to run, since the `bar_vector` has not been initialized yet.
 *
 * C++ can't change the con/destructor order, so it says "no downcalls allowed in con/destructors"
  * to get around this problem.
 *
 * The famous ISO CPP FAQ [1] recommends a workaround for this limitation: instead of calling
 * virtual methods from the constructor, make an "init()" and calling them from there.
 * [1]: https://isocpp.org/wiki/faq/strange-inheritance#calling-virtuals-from-ctors
 *
 * \code{.cpp}
 * struct Base {
 *     Base() { std::cout << "1: Base constructor\n"; foo_vector = initial_value(); }
 *     ~Base(){ std::cout << "7: Base destructor \n"; }
 *     void init() { std::cout << "3: Derived::init()\n"; bar(); }
 *     void deinit() { std::cout << "5: Derived::init()\n"; }
 *     virtual void bar() = 0;
 * };
 * struct Derived : Base {
 *     Derived() { std::cout << "2: Derived constructor\n"; bar_vector = initial_value(); }
 *     ~Derived(){ std::cout << "6: Derived destructor \n"; }
 *     virtual void bar() override {
 *         std::cout << "4: Derived::bar"
 *         bar_vector.push_back(45);
 *     }
 * };
 * 
 * Derived obj;
 * obj.init();
 * obj.deinit();
 * \endcode
 *
 * That's the beginning of a solution, but you have to remember to do it. Sometimes classes end up
 * needing to have states like constructed-but-not-inited, valid, deinited-but-not-destructed. The
 * destructor has to handle the case where the client forgot to call deinit() and the case where
 * they do call deinit().
 *
 * This makes it much easier to implement safe [RAII] patterns, because it allows one to specify
 * extra constructor-time actions.
 *
 * [1]: https://www.fluentcpp.com/2018/02/13/to-raii-or-not-to-raii/
 *
 * # Init Protocol
 *
 * This class protocol attempts to automate that solution, reducing user-burden as much as
 * possible. There is only one state of a live object: constructed-and-inited. The destructor can
 * always assume that the object has been deinited.
 *
 * It requires classes to abide by certain contracts:
 *   1. They must define a `__init_protocol_init()`, which:
 *     1. calls `init()`
 *     2. Calls `field.__init_protocol_init()` for each field in `__init_protocol_init_get_fields()`.
 *     3. `static_cast<parent>(*this).__init_protocol_init()`, if it is defined.
 *   2. They must define `__init_protocol_deinit()` which is the same as `__init_protocol_init`, but with `init`
 *      replaced by `deinit`, and the order of calls reversed (including the order of calls to init
 *      fields).
 *   3. They must define `__init_protocol_add_field()`.
 *
 * ## Simple Usage: Just one class on the inheritance chain needs init protocol
 *
 * For simple cases, the base class should inherit
 * `RequiresInitProtocol<Base>` and have a virtual destructor. The
 * bottommost derived class should be used with
 * `ProvidesInitProtocol` and have a virtual destructor.
 *
 * \code{.cpp}
 * struct Base : public RequiresInitProtocol<Base> {
 *     Base() { std::cout << "1: Base constructor\n"; foo_vector = initial_value(); }
 *     virtual ~Base(){ std::cout << "7: Base destructor \n"; }
 *     void init() { std::cout << "3: Base::init()\n"; bar(); }
 *     void deinit() { std::cout << "5: Base::init()\n"; }
 *     virtual void bar() = 0;
 * };
 * struct Derived : Base {
 *     Derived() { std::cout << "2: Derived constructor\n"; bar_vector = initial_value(); }
 *     virtual ~Derived(){ std::cout << "6: Derived destructor \n"; }
 *     virtual void bar() override {
 *         std::cout << "4: Derived::bar"
 *         bar_vector.push_back(45);
 *     }
 * };
 * 
 * ProvidesInitProtocol<Derived> obj;
 * // No need to explicitly call init() or deinit().
 * \endcode
 *
 *
 * ## Less Simple Usage: a field requires initialization after its class.
 *
 * \code{.cpp}
 * struct Field : public RequiresInitProtocol<Field> {
 *     Field(int n) { std::cout << "1: Field constructor\n"; }
 *     virtual ~Field(){ std::cout << "8: Field destructor \n"; }
 *     void init() { std::cout << "4: Field::init()\n"; }
 *     void deinit() { std::cout << "5: Field::init()\n"; }
 * };
 *
 * struct Aggregate : public RequiresInitProtocol<Base> {
 *     InitProtocolField<Field, Aggregate> field;
 *     Aggregate()
 *         : field(*this, 34) // Note that field's constructor must be called with an initial "*this"
 *     {
 *         std::cout << "2: Aggregate constructor\n";
 *     }
 *     virtual ~Aggregate(){ std::cout << "7: Aggregate destructor \n"; }
 *     void init() { std::cout << "3: Aggregate::init()\n"; }
 *     void deinit() { std::cout << "6: Aggregate::init()\n"; }
 * };
 * 
 * ProvidesInitProtocol<Aggregate> obj;
 * \endcode
 *
 * ## Complex Usage: Multiple classes on one inheritance chain need init protocol
 *
 * Suppose C inherits B inherits A, and multiple of those classes need
 * an init/deinit method. Then they should be rewritten like:
 *
 * \code{.cpp}
 * class A : public RequiresInitProtocol<A> { virtual ~A(); };
 * class B : public A, public RequiresInitProtocol<B, A> {
 *     // I would like to find a way to render these two lines obsolete, but I can't find one.
 *     using RequiresInitProtocol<B, A>::__init_protocol_init;
 *     using RequiresInitProtocol<B, A>::__init_protocol_deinit;
 *     virtual ~B();
 * };
 * class C : public B, public RequiresInitProtocol<C, B> {
 *     using RequiresInitProtocol<C, B>::__init_protocol_init;
 *     using RequiresInitProtocol<C, B>::__init_protocol_deinit;
 *     virtual ~C();
 * };
 * \end{code}
 *
 * Base class constructors go base before derived, then init() goes in the opposite order. This lets
 * you call virtual methods on the derived class just-after the constructor. Although it uses
 * multiple inheritance, it only supports single-inheritance.
 *
 * See the test-case below.
 *
 * ## Ninja Usage: Classes on different inheritance chains (through multiple inheritence) need init protocol
 *
 * In special cases (such as multiple inheritance), this class will
 * not work. In that case, you should write a class that looks like:
 *
 * \code{.cpp}
 * class MyClass_ManuallyRequiresInitProtocol : public BaseClass1, public BaseClass2, ... {
 *     void __init_protocol_init() {
 *         init();
 *         // Propagate to base classes after calling this class's init() in a user-specified order.
 *         static_cast<BaseClass1&>(*this).init();
 *         static_cast<BaseClass2&>(*this).init();
 *         // ...
 *     }
 *     void __init_protocol_deinit() {
 *         // like __init_protocol_init(), but propagate BEFORE calling this class's deinit().
 *         // Unfortunately, the rule of "propagate before deinit" and "propagate after init" have to be painstakingly remembered manually.
 *         // I would like to find a way which eliminates that.
 *     }
 *     virtual void satisfies_init_protocol() = 0;
 * }
 * \endcode
 */

namespace detail {
	template <typename T>
	class has_init {
		typedef char one;
		struct two { char x[2]; };

		template <typename C> static one test( decltype(&C::__init_protocol_init) );
		template <typename C> static two test(...);

	public:
		enum { value = sizeof(test<T>(nullptr)) == sizeof(char) };
	};

	template <typename T>
	class has_deinit {
		typedef char one;
		struct two { char x[2]; };

		template <typename C> static one test( decltype(&C::__init_protocol_init) );
		template <typename C> static two test(...);

	public:
		enum { value = sizeof(test<T>(nullptr)) == sizeof(char) };
	};

	/**
	 * @brief Call __init_protocol_init(), if BaseClass supports it.
	 */
	template<class ThisClass, class BaseClass>
	typename std::enable_if<has_init<BaseClass>::value>::type
	maybe_init(ThisClass& obj) {
		static_cast<BaseClass&>(obj).__init_protocol_init();
	}
	template<class ThisClass, class BaseClass>
	typename std::enable_if<!has_init<BaseClass>::value>::type
	maybe_init(ThisClass&) { }

	/**
	 * @brief Call __init_protocol_deinit(), if BaseClass supports it.
	 */
	template<class ThisClass, class BaseClass>
	typename std::enable_if<has_deinit<BaseClass>::value>::type
	maybe_deinit(ThisClass& obj) {
		static_cast<BaseClass&>(obj).__init_protocol_deinit();
	}
	template<class ThisClass, class BaseClass>
	typename std::enable_if<!has_deinit<BaseClass>::value>::type
	maybe_deinit(ThisClass&) { }
}

/**
 * This is a helper base class. It automates the following:
 * 1. Propagating the __init_protocol_init/deinit to BaseClass
 * 2. Calling init/deinit of ThisClass.
 * 3. Requiring the protocol tag.
 *
 * See header-level docs for usage.
 */
template <typename ThisClass, typename BaseClass = int>
class RequiresInitProtocol {
public:
	/*
	  I use SFINAE to test if the BaseClass has a __init_protocol_init attribute.
	  If it does, I need to chain main onto it.
	 */
	void __init_protocol_init() {
		static_cast<ThisClass&>(*this).init();
		for (const std::function<void()>& init : inits) {
			init();
		}
		detail::maybe_init<ThisClass, BaseClass>(dynamic_cast<ThisClass&>(*this));
	}

	void __init_protocol_deinit() {
		detail::maybe_deinit<ThisClass, BaseClass>(dynamic_cast<ThisClass&>(*this));
		for (auto deinit = deinits.crbegin(); deinit != deinits.crend(); deinit++) {
			(*deinit)();
		}
		static_cast<ThisClass&>(*this).deinit();
	}

	void __init_protocol_add_field(std::function<void()> init, std::function<void()> deinit) {
		inits.push_back(init);
		deinits.push_back(deinit);
	}

	void init() {}

	void deinit() {}

	/**
	 * Override this with a no-op, only if you fulfill this contract: you call _init() after
	 * the derived class constructor and __init_protocol_deinit() before the derived class destructor.
	 *
	 * Since failure to override this indicates you don't fulfill this contract, the compiler will
	 * tell you your class cannot be constructed until you do (it's like a type tag). Unfortunately,
	 * the compiler cannot test if you fulfill the contract, just if you claim to.
	 */
	virtual void satisfies_init_protocol() = 0;

	virtual ~RequiresInitProtocol() { }

private:
	std::vector<std::function<void()>> inits;
	std::vector<std::function<void()>> deinits;
};

template <typename ThisClass, typename AggregateClass>
class InitProtocolField final : public ThisClass {
public:
	template <class... T>
	InitProtocolField(AggregateClass& othis, T&&... t)
		: ThisClass(t...)
	{
		// Last constructor to run.
		othis.__init_protocol_add_field([this]{static_cast<ThisClass&>(*this).init();}, [this]{static_cast<ThisClass&>(*this).deinit();});
	}

	virtual void satisfies_init_protocol() { }

	const ThisClass& operator*() const { return static_cast<ThisClass&>(*this); }
	ThisClass& operator*() { return static_cast<ThisClass&>(*this); }

	const ThisClass* operator->() const { return &**this; }
	ThisClass* operator->() { return &**this; }
};

/*
 * This class provides the init_protocol by being a "mixin" class [1].  It kicks off the
 * "__init_protocol_init/deinit" chain at the first level, and the classes themselves take care of
 * propagating it upwards.
 *
 * [1]: https://www.fluentcpp.com/2017/12/12/mixin-classes-yang-crtp/
 *
 * This class is final, so it can't be subclassed. That means it is at the bottom of the inheritance
 * chain.
 */
template <typename ThisClass>
class ProvidesInitProtocol final : public ThisClass {
public:
	template <class... T>
	ProvidesInitProtocol(T&&... t)
		: ThisClass(t...)
	{
		// Last constructor to run.
		detail::maybe_init<ProvidesInitProtocol<ThisClass>, ThisClass>(*this);
	}

	~ProvidesInitProtocol() {
		// First destructor to run.
		detail::maybe_deinit<ProvidesInitProtocol<ThisClass>, ThisClass>(*this);
	}

	const ThisClass& operator*() const { return static_cast<ThisClass&>(*this); }
	ThisClass& operator*() { return static_cast<ThisClass&>(*this); }

	const ThisClass* operator->() const { return &**this; }
	ThisClass* operator->() { return &**this; }

protected:
	virtual void satisfies_init_protocol() { }
};

template <typename ThisClass>
class ManualInitProtocol final : public ThisClass {
public:
	template <class... T>
	ManualInitProtocol(T&&... t)
		: ThisClass(t...)
	{ }

	~ManualInitProtocol() { }

	const ThisClass& operator*() const { return static_cast<ThisClass&>(*this); }
	ThisClass& operator*() { return static_cast<ThisClass&>(*this); }

	const ThisClass* operator->() const { return &**this; }
	ThisClass* operator->() { return &**this; }

protected:
	virtual void satisfies_init_protocol() { }
};
