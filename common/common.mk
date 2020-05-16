# Using ?= makes these variables overridable
# Simply define them before including common.mk
STDCXX ?= c++17
DBG_FLAGS ?= -Og -g
OPT_FLAGS ?= -O3 -DNDEBUG
CPP_FILES ?= $(shell find . -name '*.cpp' -not -name 'plugin.cpp' -not -name 'main.cpp')
HPP_FILES ?= $(shell find . -name '*.hpp')

# In the future, if compilation is slow, we can enable partial compilation of object files with
#  $(OBJ_FILES:.o=.dbg.o) and  $(OBJ_FILES:.o=.opt.o)
plugin.dbg.so: plugin.cpp $(CPP_FILES) $(HPP_FILES)
	$(CXX) $(CFLAGS) $(CPPFLAGS) $(LDFLAGS) $(dbg_flags) -std=$(STDCXX) -shared -fpic -o $@ plugin.cpp $(CPP_FILES)

plugin.opt.so: plugin.cpp $(OTHER_DEPS)
	$(CXX) $(CFLAGS) $(CPPFLAGS) $(LDFLAGS) $(opt_flags) -std=$(STDCXX) -shared -fpic -o $@ plugin.cpp $(CPP_FILES)

main.dbg.exe: main.cpp $(CPP_FILES) $(HPP_FILES)
	$(CXX) $(CFLAGS) $(CPPFLAGS) $(LDFLAGS) $(dbg_flags) -std=$(STDCXX) -o $@ main.cpp $(CPP_FILES)

main.opt.exe: main.cpp $(CPP_FILES) $(HPP_FILES)
	$(CXX) $(CFLAGS) $(CPPFLAGS) $(LDFLAGS) $(opt_flags) -std=$(STDCXX) -o $@ main.cpp $(CPP_FILES)

%.dbg.o: %.cpp $(OTHER_DEPS)
	$(CXX) $(CFLAGS) $(CPPFLAGS)$(dbg_flags) -std=$(STDCXX) -o $@ $<

%.opt.o: %.cpp $(OTHER_DEPS)
	$(CXX) $(CFLAGS) $(CPPFLAGS)$(opt_flags) -std=$(STDCXX) -o $@ $<

.PHONY: clean
clean:
	touch _target && \
	$(RM) _target *.so *.exe *.o
# if *.so and *.o do not exist, rm will still work, because it still receives an operand (target)
