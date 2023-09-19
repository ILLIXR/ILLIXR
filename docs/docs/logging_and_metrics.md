# Logging and Metrics

The ILLIXR project supports several ways for an ILLIXR application to log and report details about
    its execution.

## Logging
ILLIXR uses the [spdlog](https://github.com/gabime/spdlog) library for logging. Logging goes to both `STDOUT` and one or more log files in `$ILLIXR_ROOT/logs/`

Available levels, from low to high are: `trace`, `debug`, `info`, `warn`, `error`, `critical`, `off`.

If `NDEBUG` is not defined, then the default logging level is `warn`, otherwise it is `debug`.

Logging is activated by exporting environment variables to a particular level before running ILLIXR. These take the form of `<PLUGIN_NAME>_LOG_LEVEL`, e.g.,

```
# Activate logging for both the ground_truth_slam plugin and the ILLIXR app
# Each will log to the console, with color (actual colors dependent on the terminal settings)
# Each log will write to $ILLIXR_ROOT/logs/<plugin_name>.log
# Each log can have a different level.

export GROUND_TRUTH_SLAM_LOG_LEVEL=debug
export ILLIXR_LOG_LEVEL=warn

main.dbg.exe -yaml=profiles/native_gl.yaml
``` 

When writing a new plugin, the `plugin.spdlogger(std::string log_level)` method should be called, e.g., using `std::getenv("<PLUGIN_NAME>_LOG_LEVEL")` This creates a logger with two sinks (console and file). This logger is then registered in the global spdlog registry. 

To log inside of a plugin method, use the plugin's name attribute to get the particular logger from the registry and call the desired log level method, e.g.
```
spdlog::get(name)->info("informative message");
```

Outside of the plugin class hierarchy, one can use the global ILLIXR logger which is registered under "illixr", e.g., `spdlog::get("illixr")`.  It will look for `$ILLIXR_LOG_LEVEL` in the environment or use `warn` by default. This usage requires explicitly adding the name of the component or file to the output message, if desired.

Log files are appended. To merge to a single log do `$ cat *log | sort > combined.log` This will sort correctly because the entries start with an ISO-8601 timestamp. For this reason, if a plugin uses `spdlog::set_pattern()` to create a custom log pattern, it is highly recommended that the custom pattern start with an ISO-8601 timestamp and it is required to reset to the default log message pattern after use.

### Note about #ifndef NDEBUG/#endif blocks
Many of the plugins contain their logging statements inside of blocks which are only active when doing a debug build. This is a historical artifact. New plugins should carefully consider the difference between logging a debug message and conditionally compiling blocks of code based on build type.


## Metrics

ILLIXR allows users to generate higher order statistics from logged results called _Metrics_.

***TODO***


[//]: # (- Internal -)

[20]:	glossary.md#sqlite
