# Building ILLIXR

## Basic usage

We have a tool called `runner.sh` that downloads, compiles, and runs ILLIXR. This is necessary,
since ILLIXR has plugins and data are in many different places. The tool consumes a config YAML file
which specifies those places.

To run ILLIXR natively, use

```
./runner.sh configs/native.yaml
```

To drop into `gdb`, add `command: gdb -q --args %a` in the `loader` block of `configs/native.yaml`, and use the same command.

To run ILLIXR with Monado,

```
./runner.sh configs/monado.yaml
```

The OpenXR application to run is defined in `loader.openxr_app`.

## Config file

- See `config/{native,ci,native-ground-truth,monado}.yaml`.

The first block in the config file contains a list of `plugin_groups`, where each `plugin_group` is a list of plugins.

```yaml
plugin_groups:
  - plugin_group:
      - path: plugin1/
      - path: plugin2/
      - path: plugin3/
      - path: plugin4/
```

This defines a list of plugins by their location, `path`. Allowed paths will be described below. The
`plugin_groups` get flattened and those plugins are initialized _in order_ at runtime. Several of
the default plugins are order-sensitive.

The next block in the config defines the offline IMU data, camera data, and ground-truth data.

```yaml
data:
  subpath: mav0
  relative_to:
    archive_path:
      download_url: 'http://robotics.ethz.ch/~asl-datasets/ijrr_euroc_mav_dataset/vicon_room1/V1_02_medium/V1_02_medium.zip'
```

Next, we define the location of OBJ files for `gldemo`.

```yaml
demo_data: demo_data/
```

Currently we support the following loaders: `native` (which runs ILLIXR in standalone mode), `tests`
(which runs integreation tests headlessly for CI/CD purposes), and `monado` (which runs an OpenXR
application in Monado using ILLIXR as a backend).

```yaml
loader:
  name: native
  command: gdb -q --args %a
```

The `native` loader supports an optional `command` argument. In that argument `%a` is replaced with
the separated command-line arguments to run ILLIR, while `%b` is replaced with the stringified
command-line arguments.

Finally, we support two profiles: `opt`, which compiles with `-O3` and disables debug prints, and
`dbg`, which compiles with debug flags and enables debug prints.

```yaml
profile: opt
```

You can `!include` other YAML files ([documentation][8]). Consider separating the site-specific
configuration options into its own file.

## Specifying Paths

A path refers to a location of a resource. There are 5 ways of specifying a path:

- **Simple path**: either absolute or relative path in the native filesystem.
- **Git repo**: A git repository.
```yaml
- git_repo: https://github.com/user/repo.git
  version: master # branch name, SHA-256, or tag
```
- **Download URL**: A resource downloaded from the internet.
```yaml
- download_url: https://example.com/file.txt
```
- **Zip archive**: A path that points within the contents of a zip archive. Note that `archive_path` is itself a path (recursive).
```yaml
- archive_path: path/to/archive.zip
- archive_path:
    download_url: https://example.com/file.zip
```
- **Complex path**: A hard-coded path relative to another path (recursive). This is useful to specify a _subdirectory_ of a git repository or zip archive.
```yaml
- subpath: path/within/git_repo
  relative_to:
    git_repo: ...
    version: ...
```

## Rationale

- Previously, we would have to specify which plugins to build and which to run separately, violating
  [DRY principle][7].

- Previously, configuration had to be hard-coded into the component source code, or passed as
  parsed/unparsed as strings in env-vars on a per-component basis. This gives us a consistent way to
  deal with all configurations.

- Currently, plugins are specificed by a path to the directory containing their source code and
  build system.

[7]: https://en.wikipedia.org/wiki/Don%27t_repeat_yourself
[8]: https://pypi.org/project/pyyaml-include/

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
