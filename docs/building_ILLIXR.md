# Building ILLIXR

_It may not be perfect, but it is the product of our blood, sweat, tears, and dreams._

## Basic usage

- From the project root, `make run.dbg -j$(nproc)` will build the ILLIXR runtime, the ILLIXR
  plugins, and run it for you.

- `make run.opt -j$(nproc)`, is the same but with optimizations on and debug off.

## Adding in a new plugin (the common case)

In the common case, one need only define a `Makefile` with the line `include common/common.mk` and
symlink common (`ln -s ../common common`). This provides the necessary targets and uses the compiler
`$(CXX)`, which is defined in Make based on the OS and environment variables.

- It compiles `plugin.cpp` and any other `*.cpp` files into the plugin.

- It will invoke a recompile the target any time any `*.hpp` or `*.cpp` file changes.

- It compiles with C++17. You can change this in your plugin by defining `STDCXX = ...` before the
  `include`. This change will not affect other plugins; just yours.

- Libraries can be added by appending to `LDFLAGS` and `CFLAGS`, for example

        LDFLAGS := $(LDFLAGS) $(shell pkg-config --ldflags eigen3)
        CFLAGS := $(CFLAGS) $(shell pkg-config --cflags eigen3)

- See the source for the exact flags.

## Adding in a new plugin (the general case)

Each plugin can have a completely independent build system, as long as:
- It defines a `Makefile` with targets for `plugin.dbg.so`, `plugin.opt.so`, and `clean`. Inside
  this `Makefile`, one can defer to another build system.

- It's compiler maintains _ABI compatibility_ with the compilers used in every other plugin. Using
  the same version of Clang or GCC on the same architecture is sufficient for this.

- It's name is inserted in the root `Makefile`, in the `plugins` list.

## Philosophy

- Each plugin should not have to know or care how the others are compiled. In the future, they may
  even be distributed separately, just as SOs. Therefore, each plugin needs its own build system.

- Despite this per-plugin flexibility, building the 'default' set of ILLIXR plugins should be
  extremely easy.

- It should be easy to build in parallel.

- Always rebuild every time, so the binary is always "fresh". This is a great convenience when
  experimenting. However, this implies that rebuilding must be fast when not much has changed.

- Make is the de facto standard for building C/C++ programs. GNU Make, reucrsive make, and the
  makefile language begets no shortage of problems [[1][1],[2][2],[3][3],[4][4],[5][5]], but I chose
  Make for its tradeoff of between simplicity and functionality. What it lacks in functionality
  (compared to CMake, Ninja, scons, Bazel, Meson) it makes up for in simplicity. It's still the
  build system in which it is the easiest to invoke arbitrary commands in shell and the easiest to
  have a `common.mk` included in each plugin. This decision to use Make should be revisited, when
  this project outgrows its ability, but for now, Make remains, in my judgement, _the best tool for
  the job_.

[1]: https://www.conifersystems.com/whitepapers/gnu-make/
[2]: https://www.gnu.org/software/cons/stable/cons.html#why%20cons%20why%20not%20make
[3]: https://interrupt.memfault.com/blog/gnu-make-guidelines#when-to-choose-make
[4]: https://grosskurth.ca/bib/1997/miller.pdf "Recursive Make Considered Harmful (AUUGN Journal of AUUG Inc. 1998)"
[5]: https://doi.org/10.1145/3241625.2976011 "Non-recursive make considered harmful: build systems at scale (SIGPLAN 2016)"
