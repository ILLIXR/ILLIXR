import re
import csv
from collections import defaultdict
from pathlib import Path
from typing import Iterable, List, Any
import pandas as pd

def log_to_csv(path: Path) -> Iterable[List[Any]]:
    with open(path) as f:
        for line in f:
            if re.match(r"^record,", line):
                yield line

records = defaultdict(list)
for row in csv.reader(log_to_csv("../output.log")):
    records[row[1]].append(row[2:6])
records = {
    record_type: pd.DataFrame(lst)
    for record_type, lst in records.items()
}

# I am assigning the column names here for convenience
records['start_callback_record'].columns = ["component_id", "topic_id", "serial_no", "cpu_time"]
records['stop_callback_record'].columns = ["component_id", "topic_id", "serial_no", "cpu_time"]
records['start_iteration_record'].columns = ["component_id", "iteration_no", "skip_no", "cpu_time"]
records['stop_iteration_record'].columns = ["component_id", "iteration_no", "skip_no", "cpu_time"]

total_callback_records = pd.merge(records['start_callback_record'], records['stop_callback_record'], left_on=["component_id", "serial_no"], right_on=["component_id", "serial_no"])
# Note: erros="coerce "will use NaN where string is not a valid integer
# Therefore, I will mask out the NaNs with fillna
# This will probably only knock out a few records, so it is safe.
total_callback_records["cpu_time_y"] = pd.to_numeric(total_callback_records["cpu_time_y"], errors="coerce")
total_callback_records["cpu_time_x"] = pd.to_numeric(total_callback_records["cpu_time_x"], errors="coerce")
total_callback_records["total_cpu_time"] = (total_callback_records["cpu_time_y"] - total_callback_records["cpu_time_x"]).fillna(0)
print(total_callback_records.groupby("component_id").sum("total_cpu_time")[["total_cpu_time"]])
print(total_callback_records.groupby("component_id").count()[["total_cpu_time"]].rename(columns={"total_cpu_time": "count"}))

total_iteration_records = pd.merge(records['start_iteration_record'], records['stop_iteration_record'], left_on=["component_id", "iteration_no", "skip_no"], right_on=["component_id", "iteration_no", "skip_no"])
# Note: erros="coerce "will use NaN where string is not a valid integer
# Therefore, I will mask out the NaNs with fillna
# This will probably only knock out a few records, so it is safe.
total_iteration_records["cpu_time_y"] = pd.to_numeric(total_iteration_records["cpu_time_y"], errors="coerce")
total_iteration_records["cpu_time_x"] = pd.to_numeric(total_iteration_records["cpu_time_x"], errors="coerce")
total_iteration_records["total_cpu_time"] = (total_iteration_records["cpu_time_y"] - total_iteration_records["cpu_time_x"]).fillna(0)
print(total_iteration_records.groupby("component_id").sum("total_cpu_time")[["total_cpu_time"]])
print(total_iteration_records.groupby("component_id").count()[["total_cpu_time"]].rename(columns={"total_cpu_time": "count"}))
