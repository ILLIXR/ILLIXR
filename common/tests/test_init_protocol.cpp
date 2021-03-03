#include <gtest/gtest.h>
#include "../init_protocol.hpp"

class InitProtocolTest : public ::testing::Test { };


// Works, even at top-level
struct A;
struct A : public RequiresInitProtocol<A> {
	A();
	virtual ~A();
	void init();
	void deinit();
};

// The propagation "skips a generation;" no problem.
struct B : public A {
	B();
	virtual ~B();
};

// C will be used as an "InitField" later on
struct C : public RequiresInitProtocol<C> {
	C();
	virtual ~C();
	void init();
	void deinit();
};

struct D : public B, public RequiresInitProtocol<D, B> {
	using RequiresInitProtocol<D, B>::__init_protocol_init;
	using RequiresInitProtocol<D, B>::__init_protocol_deinit;
	using RequiresInitProtocol<D, B>::__init_protocol_add_field;
	D();
	virtual ~D();
	void init();
	void deinit();
	InitProtocolField<C, D> c;
};

static std::size_t test_counter = 0;

// Here's the moment of truth.
// I've initialized an object, and now I'm going to check the order of con/destructors and de/init.

// vvvv
A::A() { EXPECT_EQ(test_counter++, 0); }
B::B() { EXPECT_EQ(test_counter++, 1); }
C::C() { EXPECT_EQ(test_counter++, 2); }
D::D() : c{*this} { EXPECT_EQ(test_counter++, 3); }
void D::init() { EXPECT_EQ(test_counter++, 4); }
void C::init() { EXPECT_EQ(test_counter++, 5); }
void A::init() { EXPECT_EQ(test_counter++, 6); }
// ==== imagine there's a horizontal mirror here
void A::deinit() { EXPECT_EQ(test_counter++, 7); }
void C::deinit() { EXPECT_EQ(test_counter++, 8); }
void D::deinit() { EXPECT_EQ(test_counter++, 9); }
D::~D() { EXPECT_EQ(test_counter++, 10); }
C::~C() { EXPECT_EQ(test_counter++, 11); }
B::~B() { EXPECT_EQ(test_counter++, 12); }
A::~A() { EXPECT_EQ(test_counter++, 13); }
// ^^^^^

TEST_F(InitProtocolTest, TestOrder) {
	ProvidesInitProtocol<D> obj;
}
