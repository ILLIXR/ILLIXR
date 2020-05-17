# Using ?= makes these variables overridable
# Simply define them before including common.mk
CXX ?= clang++
STDCXX ?= c++17
DBG_FLAGS ?= -Og -g
OPT_FLAGS ?= -O3 -DNDEBUG
CPP_FILES ?= $(shell find . -name '*.cpp' -not -name 'plugin.cpp' -not -name 'main.cpp')
HPP_FILES ?= $(shell find . -name '*.hpp')

# In the future, if compilation is slow, we can enable partial compilation of object files with
#  $(OBJ_FILES:.o=.dbg.o) and  $(OBJ_FILES:.o=.opt.o)
plugin.dbg.so: plugin.cpp $(CPP_FILES) $(HPP_FILES) Makefile
	$(CXX) -ggdb -std=$(STDCXX) $(CFLAGS) $(CPPFLAGS) $(dbg_flags) -shared -fpic -o $@ plugin.cpp $(CPP_FILES) $(LDFLAGS)

plugin.opt.so: plugin.cpp $(CPP_FILES) $(HPP_FILES) Makefile
	$(CXX) -std=$(STDCXX) $(CFLAGS) $(CPPFLAGS) $(opt_flags)  -shared -fpic -o $@ plugin.cpp $(CPP_FILES) $(LDFLAGS)

main.dbg.exe: main.cpp $(CPP_FILES) $(HPP_FILES) Makefile
	$(CXX) -ggdb -std=$(STDCXX) $(CFLAGS) $(CPPFLAGS) $(dbg_flags) -o $@ main.cpp $(CPP_FILES) $(LDFLAGS)

main.opt.exe: main.cpp $(CPP_FILES) $(HPP_FILES) Makefile
	$(CXX) -std=$(STDCXX) $(CFLAGS) $(CPPFLAGS) $(opt_flags) -o $@ main.cpp $(CPP_FILES) $(LDFLAGS)

%.dbg.o: %.cpp $(OTHER_DEPS) Makefile
	$(CXX) -std=$(STDCXX) $(CFLAGS) $(CPPFLAGS) $(dbg_flags) -o $@ $<

%.opt.o: %.cpp $(OTHER_DEPS) Makefile
	$(CXX) -std=$(STDCXX) $(CFLAGS) $(CPPFLAGS) $(opt_flags) -o $@ $<

.PHONY: clean
clean:
	touch _target && \
	$(RM) _target *.so *.exe *.o
# if *.so and *.o do not exist, rm will still work, because it still receives an operand (target)
