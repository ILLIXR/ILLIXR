# Logging and Metrics

The ILLIXR project supports several ways for an ILLIXR application to log and report details about
    its execution.


## Logging

ILLIXR implements a modular logging system that enables users to capture and record key statistics
	in real-time.

-	**`record_logger`**:
	The base class describing ILLIXR's logging interface.

-	**`noop_logger`**:
	Implements a trivially empty implementation of `record_logger`.
	Can be used for debugging or performance if runtime statistics are not needed.

-	**`sqlite_record_logger`**:
	Extends the `record_logger` to store records in a local [_SQLite database_][20].


## Metrics

ILLIXR allows users to generate higher order statistics from logged results called _Metrics_. The relevant public API is given in common/cpu_timer/cpu_timer.hpp.

To generate metrics:
- At the top of main() call setup_frame_logger() from runtime/frame_logger2.cpp
- Metrics are collected by inserting one of the CPU_TIMER_TIME_* macros defined in common/cpu_timer/cpu_timer.hpp at the location of interest.
- A  _BLOCK is recorded from the macro until the end of the enclosing scope.
- An _EVENT is recorded as an instaneous event.
- Metrics can be disabled at compile time by defining CPU_TIMER_DISABLE.
- Metrics are written to sqlite3 files in $ILLIXR_ROOT/metrics/frames via the sqlite_record_logger mechanism.  

[//]: # (- Internal -)

[20]:	glossary.md#sqlite
