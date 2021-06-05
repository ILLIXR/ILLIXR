# Getting Started

## ILLIXR Runtime

These instructions have been tested with Ubuntu 18.04 and 20.04.

1. Clone the repository:

        git clone --recursive --branch v2-latest https://github.com/ILLIXR/ILLIXR


2. Update the submodules. Submodules are git repositories inside a git repository that need to be
   pulled down separately:

        git submodule update --init --recursive

3. Install dependencies. This script installs some Ubuntu/Debian packages and builds several dependencies
   from source:

        ./install_deps.sh

4. Inspect `configs/native.yaml`. The schema definition is in `runner/config_schema.yaml`. For more
   details on the runner and the config files, see [Building ILLIXR][6].

5. Build and run ILLIXR standalone:

        ./runner.sh configs/native.yaml

6. If so desired, you can run ILLIXR headlessly using [xvfb][5]:

        ./runner.sh configs/headless.yaml

## ILLIXR Runtime with Monado (supports OpenXR)

ILLIXR leverages [Monado][3], an open-source implementation of [OpenXR][4], to support a wide range
of applications. Because of a low-level driver issue, Monado only supports Ubuntu 18.04+.

6. Compile and run:

        ./runner.sh configs/monado.yaml

## ILLIXR Standalone

ILLIXR can also benchmark each component in isolation.

1. Clone the repository.

        git clone --recursive --branch v1-latest https://github.com/ILLIXR/ILLIXR

2. Update the submodules. Submodules are git repositories inside a git repository that need to be
   pulled down separately.

        git submodule update --init --recursive

3. Each component is a directory in `benchmark`. See those components for their documentation.

## Virtual Machine

ILLIXR can be run inside a Qemu-KVM image. Check out the instructions [here][7].

## ILLIXR Scheduler experiments

These are the instructions for running the scheduler experiment. This has **not** been tested yet.

1. Run `./install_deps.sh`. Save time by saying "no" to the items you have already done. All of the new ones are at the end. You will need to install apt deps again.

2. Check out a specific version. Replace `${branch}` with either `checkpoint-sosp` (tag frozen in time) or `project-scheduling` (branch with the latest changes) in the following snippet. New development should begin on `project-scheduling`. After SOSP, I have worked to make `project-scheduling` run on other people's machines. You can always `git diff checkpoint-sosp project-scheduling` to see what has been improved.

```
git checkout ${branch}
git -C ../open_vins checkout ${branch}
git -C ../illixr-analysis checkout ${branch}
```

3. Verify the new `./config/native.yaml` (the other configs are out-of-date). All of the experimentally-controlled variables reside in `conditions`; they will be set by the meta-runner.

  - `loader.scheduler.nodes.timewarp_gl[0][loader.conditions.cpu_freq]` determines the vsync delay period (integer nanoseconds), in both scheduled and non-scheduled cases. The experiment is **highly** sensitive to this parameter 😥.
  - The rest of `loader.scheduler` contains parameters only used when the scheduler is `static` or `dynamic`.

4. To run one configuration, type `./runner.sh config/native.yaml`. Note that the runner has changed slightly:

  - It is easier to run with GDB, with sudo, and with other runners. See `loader` in `configs/native.yaml` for details.
  - The runner supports `--override parent_key.child_key=json_encoded_val`.
  - The runner outputs the config it actually uses (after defaults and overrides have been filled) to `metrics/`. The config and the metrics should always travel together. The analysis uses the config to aggregate group trials.
  - The runner makes it easier to run with GDB, run as sudo, etc. See `loader` in `configs/native.yaml`.
  - All of the controlled variables for our experiments are in `conditions`. They will be set by the metarunner. These include:
    - `scheduler`: switches between schedulers without recompiling. At the time of writing, `default`, `priority`, `manual`, `static`, and `dynamic` are supported. Search [1](https://docs.google.com/document/d/1FPjLn1FzxDuFla1P-VtXKRnCYHoGSGvXkcZEYO0PM7E) for their definition.
    - `duration`: the duration in seconds to run ILLIXR.
    - `cpus`: the number of CPUs to use.
    - `cpu_freq`: the CPU frequency in GHz to use

5. Invoke the meta-runner: it runs the runner over a configuration-grid. Run `./metarunner.sh --help` for info. Once you figure out the flags that you want, run them with `--dry-run`. This prints the configurations under trial without actually running them. Finally, when you have the configuration grid you like, run it without `--dry-run`. You should not touch the mouse or keyboard, lest your input perturb the results. Be sure to disable your screensaver before starting. For example:

```
./metarunner.sh --help
./metarunner.sh metrics-all --iters 2 --cpu-freqs 2.6,5.3 --swaps 2 --no-multicore-manual --dry-run
./metarunner.sh metrics-all --iters 2 --cpu-freqs 2.6,5.3 --swaps 2 --no-multicore-manual
# This takes a 75 seconds + startup/shutdown (~20s) per item in the configuration-grid...
```

  - Each run's `metrics_dir` is given a random name and moved into the `dir_of_metrics_dirs`, which is defined by the first argument to `./metarunner.sh`.
  - Following best practices, information meant to be computer-readable is not kept in filenames or directory names. Instead, we know the conditions and config from `${metrics}_dir/config.yaml`. This permits me to put more information that would fit into the filename, and I can use a computer-readable format, such as YAML. This does make it harder to manually find the metrics for a single trial, but the analysis script generates a "table of contents," so you can map "conditions" to `metrics_dir`.

6. Clone, install, and run the analysis, possibly on another machine. The analysis uses CPU parallelism, so more cores helps. Follow the instructions in [`ILLIXR-analysis/README.md`](https://github.com/ILLIXR/ILLIXR-analysis/blob/project-scheduling/README.md).

### Other changes from mainline ILLIXR

- Plugin IDs have superseded the use of plugin names, in most places. The IDs are 1-based integer indexes based on the order of `plugin_group[*].plugin_group` in `configs/*.yaml`. I regret switching away from string names; at the time Switchboard had IDs and not names 😥.

- By default, when using the static or dynamic scheduler, `threadloop` is a wrapper around a Switchboard thread subcribing to the scheduler's triggers. When not using the static or dynamic scheduler OR when `threadloop(..., self_scheduled=true)`, `threadloop` is a wrapper around a `std::thread`, like in mainline ILLIXR.

- ILLIXR must be run as root, so it can set the scheduler to FIFO. We choose to use root even for the other schedulers as well for consistency's sake.

- This branch is based on an early version of Switchboard (issue-32-fix-mem-leak), and contains early prototypes of #217 (synchronize thread starts), #58 (use a common timer), #209 (add timing infrastructure), #211 (log timing data), #220 (use manged thread), and #222 (set thread names).

- When using GDB or NSight Systems, be aware of the thread names. Self-scheduled threadloops are named `tl_${PLUGIN_ID}`, and Switchboard threads `s_${PLUGIN_ID}_${TOPIC_NAME}` where `${PLUGIN_ID}` is an integer plugin ID and `${TOPIC_NAME}` is a topic name.

- **For the documentation of the scheduler itself:** see [that project](https://github.com/aditi741997/robotics_project/tree/project-scheduling/README.md).

## Next steps

 The source code is divided into the following directories:
- `runtime`: create a runnable binary that loads every plugin.
    * This contains Spindle, which is responsible for loading plugins.

- `common`: resources one might use in each plugin. Most plugins symlink this directory into theirs.
    * Contains the interface for Switchboard, which maintains event-streams (implementation is in `runtime`).
    * Contains the interface for Phonebook, which is a service-directory (implementation is in `runtime`).

- a directory for each plugin. Almost all of the XR functionality is implemented in plugins. See
  [Default Components][1] for more details.

Try browsing the source of plugins. If you edit any of the source files, the runner will
detect and rebuild the respective binary. If you want to add your own, see [Writing Your Plugin][2].

[1]: default_plugins.md
[2]: writing_your_plugin.md
[3]: https://monado.dev/
[4]: https://www.khronos.org/openxr/
[5]: http://manpages.ubuntu.com/manpages/bionic/man1/Xvfb.1.html
[6]: building_illixr.md
[7]: https://github.com/ILLIXR/ILLIXR/blob/master/qemu/INSTRUCTIONS.md
