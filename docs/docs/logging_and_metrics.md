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

ILLIXR allows users to generate higher order statistics from logged results called _Metrics_.

***TODO***


[//]: # (- Internal -)

[20]:	glossary.md#sqlite
