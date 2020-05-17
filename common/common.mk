# Using ?= makes these variables overridable
# Simply define them before including common.mk
LIBOPENCV ?= $(pkg-config --cflags --libs opencv4) $(pkg-config opencv --cflags --libs)
STDCXX ?= c++2a
DBG_FLAGS ?= -Og -g
OPT_FLAGS ?= -O3 -DNDEBUG
OTHER_DEPS ?= $(shell find . -name '*.cpp') $(shell find . -name '*.hpp')

plugin.dbg.so: plugin.cpp $(OBJFILES:.o=.dbg.o) $(OTHER_DEPS)
	$(CXX) $(CFLAGS) $(CPPFLAGS) $(LDFLAGS) $(dbg_flags) -std=$(STDCXX) -shared -fpic -o $@ plugin.cpp $(OBJFILES:.o=.dbg.o)

plugin.opt.so: plugin.cpp  $(OBJFILES:.o=.opt.o) $(OTHER_DEPS)
	$(CXX) $(CFLAGS) $(CPPFLAGS) $(LDFLAGS) $(opt_flags) -std=$(STDCXX) -shared -fpic -o $@ plugin.cpp $(OBJFILES:.o=.opt.o)

main.dbg.exe: main.cpp $(OBJFILES:.o=.dbg.o) $(OTHER_DEPS)
	$(CXX) $(CFLAGS) $(CPPFLAGS) $(LDFLAGS) $(dbg_flags) -std=$(STDCXX) -o $@ main.cpp $(OBJFILES:.o=.dbg.o)

main.opt.exe: main.cpp  $(OBJFILES:.o=.opt.o) $(OTHER_DEPS)
	$(CXX) $(CFLAGS) $(CPPFLAGS) $(LDFLAGS) $(opt_flags) -std=$(STDCXX) -o $@ main.cpp $(OBJFILES:.o=.opt.o)

%.dbg.o: %.cpp $(OTHER_DEPS)
	$(CXX) $(CFLAGS) $(CPPFLAGS)$(dbg_flags) -std=$(STDCXX) -o $@ $<

%.opt.o: %.cpp $(OTHER_DEPS)
	$(CXX) $(CFLAGS) $(CPPFLAGS)$(opt_flags) -std=$(STDCXX) -o $@ $<

.PHONY: clean
clean:
	touch _target && \
	$(RM) _target *.so *.exe *.o
# if *.so and *.o do not exist, rm will still work, because it still receives an operand (target)
