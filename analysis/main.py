from pathlib import Path
from typing import Iterable, List, Any

import pandas as pd
import sqlite3

def read_illixr_table(table_name: str, index_cols: List[str]):
    db_path = Path("..") / "metrics" / (table_name + ".sqlite")
    assert db_path.exists()
    conn = sqlite3.connect(str(db_path))
    return (
        pd.read_sql_query(f"SELECT * FROM {table_name};", conn)
        .sort_values(index_cols)
        .set_index(index_cols)
    )

plugin_start               = read_illixr_table("plugin_start"              , ["plugin_id"])
threadloop_iteration_start = read_illixr_table("threadloop_iteration_start", ["plugin_id", "iteration_no"])
threadloop_iteration_stop  = read_illixr_table("threadloop_iteration_stop" , ["plugin_id", "iteration_no"])
threadloop_skip_start      = read_illixr_table("threadloop_skip_start"     , ["plugin_id", "iteration_no", "skip_no"])
threadloop_skip_stop       = read_illixr_table("threadloop_skip_stop"      , ["plugin_id", "iteration_no", "skip_no"])
switchboard_callback_start = read_illixr_table("switchboard_callback_start", ["plugin_id", "serial_no"])
switchboard_callback_stop  = read_illixr_table("switchboard_callback_stop" , ["plugin_id", "serial_no"])
switchboard_topic_stop     = read_illixr_table("switchboard_topic_stop"    , ["topic_name"])

def compute_durations(start: pd.DataFrame, stop: pd.DataFrame):
    ts = pd.merge(
        start,
        stop ,
        left_index =True,
        right_index=True,
        suffixes=("_start", "_stop")
    )
    ts["cpu_duration"] = ts["cpu_time_stop"] - ts["cpu_time_start"]
    ts["wall_duration"] = ts["wall_time_stop"] - ts["wall_time_start"]
    summary = ts.groupby("plugin_id").agg(dict(
        cpu_duration =["count", "mean", "std"],
        wall_duration=["count", "mean", "std"],
    ))
    summary.columns = ['_'.join(column) for column in summary.columns]
    return ts, summary

threadloop_iteration_ts, threadloop_iteration_summary = compute_durations(threadloop_iteration_start, threadloop_iteration_stop)
threadloop_skip_ts     , threadloop_skip_summary      = compute_durations(threadloop_skip_start     , threadloop_skip_stop     )
switchboard_callback_ts, switchboard_callback_summary = compute_durations(switchboard_callback_start, switchboard_callback_stop)

for summary in [threadloop_iteration_summary, threadloop_skip_summary, switchboard_callback_summary]:
    print(pd.merge(plugin_start[["plugin_name"]], summary, left_index=True, right_index=True))
    print("\n")
