# Logging and Metrics

The ILLIXR project supports several ways for an ILLIXR application to log and report details about
    its execution.

## Logging
ILLIXR uses [spdlog](https://github.com/gabime/spdlog) for logging. Logging goes to both `STDOUT` and one or more log files in `$ILLIXR_ROOT/logs/`

Available levels, from low to high are: `trace`, `debug`, `info`, `warn`, `error`, `critical`. Also available and used as the default: `off`.

Logging is activated by exporting environment variables to a particular level before running ILLIXR. These take the form of `<PLUGIN_NAME>_LOG_LEVEL`, e.g.,

```
# Activate logging for both the ground_truth_slam plugin and the ILLIXR app
# Each will log to the console, with color (actual colors dependent on the terminal settings)
# Each log will write to $ILLIXR_ROOT/logs/<plugin_name>.log
# Logs can have different levels.

export GROUND_TRUTH_SLAM_LOG_LEVEL=debug
export ILLIXR_APP_LOG_LEVEL=warn
...
``` 

When writing a new plugin, the `plugin.spdlogger()` method should be called with the argument: `ILLIXR::getenv_or("<PLUGIN_NAME>_LOG_LEVEL","<default>")` This creates a logger with two sinks (console and file). This logger is then registered in the global spdlog registry. 

To log inside of a plugin method, use the plugin's name attribute to get the particular logger from the registry and call the desired log level method, e.g.
```
spdlog::get(name)->info("informative message");
```

To get the ILLIXR app logger use `spdlog::get("illixr_app")`, for example when logging outside of a plugin method. This usage requires explicitly adding the name of the component or file to the output message, if desired.

Log files are appended. To merge to a single log do `$ cat *log | sort > combined.log` This will sort correctly because the entries start with an ISO-8601 timestamp. For this reason, if a plugin uses `spdlog::set_pattern()` to create a custom log pattern, it is highly recommended that the custom pattern start with an ISO-8601 timestamp and it is required to reset to the default log message pattern after use.

### partial Kimera-VIO logging workaround

The ILLIXR/Kimera-VIO fork uses [glog](https://github.com/google/glog). A partial workaround is to `export GLOG_logtostderr=1` and then redirect STDERR. This will not capture `std::cout` usage in Kimera-VIO (nor in OpenCV, or other components). It will capture the plugin-build progress bar created by the Python app runner.


## Metrics

ILLIXR allows users to generate higher order statistics from logged results called _Metrics_.

***TODO***


[//]: # (- Internal -)

[20]:	glossary.md#sqlite
