# Building ILLIXR

The ILLIXR application is kick-started through a tool called [_Runner_][10]
    (found in `runner/` and `runner.sh`).
The Runner tool is responsible for
    preparing the environment,
    downloading required assets/code,
    compiling each plugin,
    and
    launching the ILLIXR application.
Runner is necessary for our project since ILLIXR manages plugins and data that span
    many locations and launch [_configurations_][11].
A configuration (defined via a [_YAML_][13] file in `ILLIXR/configs/`) specifies parameters
    and plugins required to launch ILLIXR for a specific design/evaluation scenario.


## Compilation and Usage

To run ILLIXR (from the root directory of the project) using
    the default `native` launch configuration,

<!--- language: lang-shell -->
    ./runner.sh configs/native.yaml

To run ILLIXR with Monado,

<!--- language: lang-shell -->
    ./runner.sh configs/monado.yaml

The [_OpenXR_][14] application to run is defined in `action.openxr_app`
    (a [_YAML_][13] object).
See the [Configuration][16] section for more details.


## Configuration

As introduced in the [introduction to the ILLIXR build process][12], a [_Configuration_][11]
    (or _config_) describes the key information needed to launch an ILLIXR application.
This section provides a detailed breakdown of the structure of a configuration file.
The default `ILLIXR/configs/native.yaml` for the `native` action will be used as
    the running example.

The first block in the config file (after any comments or file header) specifies
    the [_Action_][11] and any context-specific parameters for that action:

<!--- language: lang-yaml -->
    action:
      name: native

      kimera_path: !include "data/kimera-default.yaml"
      audio_path:  !include "data/audio-default.yaml"

      # run in GDB:
      # command: gdb -q --args $cmd

      # Print cmd for external use:
      # command: printf %s\n $env_cmd

      # Capture stdout for metrics
      # log_stdout: metrics/output.log

Each `action` specifies a `name` which has a corresponding function
    called from Runner (called from `ILLIXR/runner/runner/main.py`).
For actions that launch ILLIXR (like `native`), `kimera_path` is a [_Path_][15] that
    points to the root directory to look for Kimera-VIO, if the plugin is enabled.
In the example above, `kimera_path` is defined in `ILLIXR/configs/data/kimera-default.yaml`.
Actions also define `audio_path`, which points to the root directory for the audio_pipeline
    plugin, if enabled.
In the example above, `audio_path` is defined in `ILLIXR/configs/data/audio-default.yaml`.
You can `!include` other configuration files via [pyyaml-include][13].
Consider separating the site-specific configuration options into their own file.

Following `kimera_path` are a few commented parameters for debugging and logging.
The `native` action supports an optional `command` argument.
To drop into `gdb`, uncomment `command: gdb -q --args $cmd` in the `action` definition.
The `$cmd` variable is replaced with the separated command-line arguments to run ILLIXR,
    while `$quoted_cmd` is replaced with a single string comprising all command-line arguments.
The `command` argument also supports `$env_cmd`, which interpret command-line argument
    assignments in the form of `VARNAME=VALUE` as environment variable mappings.
See the [_configuration_ glossary entry][11] for more details about supported actions.

<!--- language: lang-yaml -->
    common:    !include "plugins/common.yaml"
    runtime:   !include "plugins/runtime.yaml"
    data:      !include "data/data-default.yaml"
    demo_data: !include "data/demo-default.yaml"

After the action block, additional paths are specified for various runtime parameters, including:

-   `common`: The ILLIXR common interfaces and utilities.

-   `runtime`: The ILLIXR runtime implementation.

-   `data`: The directory holding ground truth pose data, sensor data, etc.

-   `demo_data`: The directory holding 3D scene environment data.

Pre-defined paths for data assets can be found in `ILLIXR/configs/data/`.
See the [Specifying Paths][15] section for more information.

Next is the compilation `profile` property (see [_profiles_][11]) which accepts two values:

-   `opt`: Compiles with `-O3` and disables debug prints and assertions.

-   `dbg`: Compiles with debug flags and enables debug prints and assertions.

Finally, configurations that launch the ILLIXR application specify [_Flows_][12]
    of plugins to execute.
The `configs/native.yaml` file has this pre-defined flow structure:

<!--- language: lang-yaml -->
    flows:
      - !include "ci/flows/kimera-gtsam.yaml"
    append: !include "plugins/groups/misc-native.yaml"

This example includes the predefined `kimera-gtsam` flow in the `flows` array.
Pre-defined flows can be found in `ILLIXR/configs/ci/flows/`.
Each flow specifies an array of [_Plugin Groups_][12] to load.
Here, `append` is also used to add an extra plugin group to each flow
    _after_ the plugins in that flow.
Pre-defined plugin groups can be found in `ILLIXR/configs/plugins/groups/`.

You can also specify a new custom flow composing new or existing plugins:

<!--- language: lang-yaml -->
    flows:
      - flow:
        - plugin_group:          # plugin_group1
          - path: plugin1/
          - path: plugin2/
          - !include "plugins/plugin3.yaml"
          - path: plugin4/
        - !include "plugins/groups/plugin_group2.yaml"

A `plugin_group` defines an array of plugins by their `path` YAML object.
A new `path` can be described in-place as shown,
    or a pre-existing definition (found in `ILLIXR/configs/plugins`) can be used.
More than one group can be included in a `flow`,
    as shown by the included pre-existing `plugin_group2.yaml`.

For more details about adding your own plugin,
    see the [Writing your Plugin][16] page.


## Specifying Paths

A path refers to a location of a resource.
There are 5 ways of specifying a path:

-   **Simple path**:
    Either absolute or relative path in the native filesystem.

-   **Git repo**:
    A git repository.

    <!--- language: lang-yaml -->

        - git_repo: https://github.com/user/repo.git
          version: master # branch name, SHA-256, or tag

-   **Download URL**:
    A resource downloaded from the internet.

    <!--- language: lang-yaml -->

        - download_url: https://example.com/file.txt

-   **Zip archive**:
    A path that points within the contents of a zip archive.
    Note that `archive_path` is itself a path (recursive).

    <!--- language: lang-yaml -->

        - archive_path: path/to/archive.zip
        - archive_path:
            download_url: https://example.com/file.zip

-   **Complex path**:
    A hard-coded path relative to another path (recursive).
    This is useful to specify a _subdirectory_ of a git repository or zip archive.

    <!--- language: lang-yaml -->

        - subpath: path/within/git_repo
          relative_to:
            git_repo: ...
            version: ...


## Rationale

-   Previously, we would have to specify which plugins to build and which to run separately,
        violating [DRY principle][6].

-   Previously, configuration had to be hard-coded into the component source code,
        or passed as parsed/unparsed as strings in env-vars on a per-component basis.
    This gives us a consistent way to deal with all configurations.

-   Currently, plugins are specified by a path to the directory containing their source code
        and build system.


## Philosophy

-   Each plugin should not have to know or care how the others are compiled.
    In the future, they may even be distributed separately, just as SOs.
    Therefore, each plugin needs its own build system.

-   Despite this per-plugin flexibility, building the 'default' set of ILLIXR plugins
        should be extremely easy.

-   It should be easy to build in parallel.

-   Always rebuild every time, so the binary is always "fresh."
    This is a great convenience when experimenting.
    However, this implies that rebuilding must be fast when not much has changed.

-   Make is the de facto standard for building C/C++ programs.
    GNU Make, and the makefile language begets no shortage of problems
        [[1][1],[2][2],[3][3],[4][4],[5][5]], but we choose
    Make for its tradeoff of between simplicity and functionality.
    What it lacks in functionality (compared to CMake, Ninja, scons, Bazel, Meson)
        it makes up for in simplicity.
    It's still the build system in which it is the easiest to invoke arbitrary commands in
        shell and the easiest to have a `common.mk` included in each plugin.
    This decision to use Make should be revisited, when this project outgrows its ability,
        but for now, Make remains, in our judgement, _the best tool for the job_.


[//]: # (- References -)

[1]:    https://www.conifersystems.com/whitepapers/gnu-make
[2]:    https://www.gnu.org/software/cons/stable/cons.html#why%20cons%20why%20not%20make
[3]:    https://interrupt.memfault.com/blog/gnu-make-guidelines#when-to-choose-make
[4]:    https://grosskurth.ca/bib/1997/miller.pdf
        "Recursive Make Considered Harmful (AUUGN Journal of AUUG Inc. 1998)"
[5]:    https://doi.org/10.1145/3241625.2976011
        "Non-recursive make considered harmful: build systems at scale (SIGPLAN 2016)"
[6]:    https://en.wikipedia.org/wiki/Don%27t_repeat_yourself

[//]: # (- Internal -)

[10]:   glossary.md#runner
[11]:   glossary.md#configuration
[12]:   building_illixr.md#building-illixr
[13]:   glossary.md#yaml
[14]:   glossary.md#openxr
[15]:   building_illixr.md#specifying-paths
[15]:   building_illixr.md#configuration
[16]:   writing_your_plugin.md
