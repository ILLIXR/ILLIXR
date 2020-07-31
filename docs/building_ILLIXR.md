# Building ILLIXR

## Basic usage

- Edit `config.yaml`. See `runner/config_schema.yaml` for the schema definition.

  * Make sure you have defined all of the plugins you want with paths that exist.

  * Currently we support the following loaders: `native` (which runs ILLIXR in standalone mode),
    `gdb` (which runs standalone mode in GDB for debugging purposes), and `monado` (which runs an
    OpenXR application in Monado using ILLIXR as a backend)

    * If you want to run with Monado, make sure you define Monado and an OpenXR application in the
      loader (see `runner/config_schema.yaml` for specifics).

  * Paths are resolved relative to the project root.

  * You can `!include` other YAML files ([documentation][8]). Consider separating the site-specific
    configuration options into its own file.

- Run `./runner.sh config.yaml`.

  * This compiles whatever plugins and runtime code is necessary and runs the result.

  * This also sets the environment variables properly.

## Rationale

- Previously, we would have to specify which plugins to build and which to run separately, violating
  [DRY principle][7].

- Previously, configuration had to be hard-coded into the component source code, or passed as
  parsed/unparsed as strings in env-vars on a per-component basis. This gives us a consistent way to
  deal with all configurations.

- Currently, plugins are specificed by a path to the directory containing their source code and
  build system. In the future, the same config file could support HTTP URLs Git URLs
  (`git+https://github.com/username/repo@rev?path=optional/path/within/repo`), or Zip URLs
  (`zip+http://path/to/archive.zip?path=optional/path/within/zip`), or even Nix URLs (TBD).

[7]: https://en.wikipedia.org/wiki/Don%27t_repeat_yourself
[8]: https://pypi.org/project/pyyaml-include/

## Adding a new plugin (common case)

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

- Inserted the path of your directory into the `plugin`-list in `config.yaml`.

## Adding a plugin (general case)

Each plugin can have a completely independent build system, as long as:
- It defines a `Makefile` with targets for `plugin.dbg.so`, `plugin.opt.so`, and `clean`. Inside
  this `Makefile`, one can defer to another build system.

- It's compiler maintains _ABI compatibility_ with the compilers used in every other plugin. Using
  the same version of Clang or GCC on the same architecture is sufficient for this.

- It's path is inserted in the root `config.yaml`, in the `plugins` list.

## Philosophy

- Each plugin should not have to know or care how the others are compiled. In the future, they may
  even be distributed separately, just as SOs. Therefore, each plugin needs its own build system.

- Despite this per-plugin flexibility, building the 'default' set of ILLIXR plugins should be
  extremely easy.

- It should be easy to build in parallel.

- Always rebuild every time, so the binary is always "fresh". This is a great convenience when
  experimenting. However, this implies that rebuilding must be fast when not much has changed.

- Make is the de facto standard for building C/C++ programs. GNU Make, and the
  makefile language begets no shortage of problems [[1][1],[2][2],[3][3],[4][4],[5][5]], but we choose
  Make for its tradeoff of between simplicity and functionality. What it lacks in functionality
  (compared to CMake, Ninja, scons, Bazel, Meson) it makes up for in simplicity. It's still the
  build system in which it is the easiest to invoke arbitrary commands in shell and the easiest to
  have a `common.mk` included in each plugin. This decision to use Make should be revisited, when
  this project outgrows its ability, but for now, Make remains, in our judgement, _the best tool for
  the job_.

[1]: https://www.conifersystems.com/whitepapers/gnu-make/
[2]: https://www.gnu.org/software/cons/stable/cons.html#why%20cons%20why%20not%20make
[3]: https://interrupt.memfault.com/blog/gnu-make-guidelines#when-to-choose-make
[4]: https://grosskurth.ca/bib/1997/miller.pdf "Recursive Make Considered Harmful (AUUGN Journal of AUUG Inc. 1998)"
[5]: https://doi.org/10.1145/3241625.2976011 "Non-recursive make considered harmful: build systems at scale (SIGPLAN 2016)"
