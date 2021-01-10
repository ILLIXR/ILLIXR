from typing import Iterable, TypeVar, Container, Optional, List
import os
from pathlib import Path
import json
import pandas as pd


T = TypeVar("T")
def remove_all(haystack: Iterable[T], needles: Container[T]) -> Iterable[T]:
    for elem in haystack:
        if elem not in needles:
            yield elem


ending = "_data.csv"


def read_file(data_path: Path) -> pd.DataFrame:
    # First line should be JSON metadata
    with data_path.open() as data_lines:
        data_first_line = next(data_lines)
    assert data_first_line.startswith("#{")
    options = json.loads(data_first_line[1:])
    assert options["version"] == "3.2"

    data = (
        pd.read_csv(data_path, **options["pandas_kwargs"])
        .sort_index()
    )

    # Within each thread, function_id uniquely identifies a function name
    # but we only print the corresponding name on the its occurrence, to save space/memory in CSV.
    # I can achieve the same efficiency with Pandas Categorical type
    function_name_map = (
        data
        .reset_index()
        .groupby(["thread_id", "function_id"])
        .agg({"function_name": "first"})
        .assign(**{
            "function_name": lambda df: pd.Categorical(df["function_name"], ordered=True)
        })
    )

    # Check that map is filled for all (thread_id, function_id)
    if not function_name_map["function_name"].astype(str).all():
        raise RuntimeError("\n".join([
            f"No function name for (thread_id, function_id) in {data_path!s}:",
            str(list(function_name_map.index[function_name_map == '']))
        ]))

    # Change function_id -> function_name (by joining with function_name_map)
    data = (
        data
        .drop(columns=["function_name"])
        .merge(
            function_name_map,
            left_on=["thread_id", "function_id"],
            right_index=True,
            how="left",
            validate="m:1",
        )
        .drop(columns=["function_id"])
    )

    # (thread_id, stack_id) is unique within the data_file, but not within the whole program!
    # Therefore, I use (data_file, thread_id, stack_id) as the key
    data = (
        data
        .assign(**{
            "data_file": pd.Categorical([
                data_path.name[:-len(ending)]
                for _ in range(len(data))
            ], ordered=True),
        })
        .reset_index()
        .set_index(["data_file", "thread_id", "frame_id"], drop=True, verify_integrity=True)
        .sort_index()
    )

    # Rearrange the columns
    front_columns = ["function_name", "comment", "caller_frame_id"]
    return data[[*front_columns, *remove_all(data.columns, front_columns)]]


from pandas.api.types import union_categoricals
def normalize_cats(
        dfs: List[pd.DataFrame],
        columns: Optional[List[str]] = None,
        include_indices: bool = True
) -> List[pd.DataFrame]:
    if not dfs:
        return dfs

    if columns is None:
        if include_indices:
            # peel of indices into columns so that I can mutate them
            indices = [df.index.names for df in dfs]
            dfs = [df.reset_index() for df in dfs]
        columns2 = [
            column
            for column, dtype in dfs[0].dtypes.iteritems()
            if isinstance(dtype, pd.CategoricalDtype)
        ]
    else:
        columns2 = columns

    columns_union_cat = {
        column: union_categoricals([
            df[column]
            for df in dfs
        ], ignore_order=True, sort_categories=True).as_ordered()
        for column in columns2
    }

    dfs = [
        df.assign(**{
            column: pd.Categorical(df[column], categories=union_cat.categories)
            for column, union_cat in columns_union_cat.items()
        })
        for df in dfs
    ]

    if columns is None and include_indices:
        dfs = [
            df.reset_index(drop=True).set_index(index)
            for df, index in zip(dfs, indices)
        ]

    return dfs


def read_dir(path: Optional[Path] = None, no_data_err: bool = True) -> pd.DataFrame:
    path2 = Path(os.environ.get("CPU_TIMER3_PATH", ".cpu_timer3")) if path is None else path
    files = list(path2.glob("*_data.csv"))
    if files:
        data_pieces = [
            read_file(file)
            for file in files
        ]
        data_pieces = [
            data_piece
            for data_piece in data_pieces
            if not data_piece.empty
        ]
        data = pd.concat(normalize_cats(data_pieces), verify_integrity=True, sort=True)
        return data
    elif no_data_err:
        raise RuntimeError(f"No *_data.csv found in {path2!s}\n{list(path2.iterdir())}")
    else:
        return pd.DataFrame()
