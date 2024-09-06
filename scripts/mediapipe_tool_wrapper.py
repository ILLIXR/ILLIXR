#!/usr/bin/env python3
import ast
import os
import re
import glob
from typing import Dict, Any, Optional, Union, List, Tuple

# rep_list = ["name", "data", "srcs", "hdrs", "deps"]
ptrn = re.compile(r"(\S+) = ")
ptrn1 = re.compile(r"(^[a-zA-Z_0-9]*) = (.*)")

remembered_choices = []


class Target:
    def __init__(self, name: str, root: str, srcs: Optional[List[str]] = None, hdrs: Optional[List[str]] = None,
                 deps: Optional[List[str]] = None, select: Optional[List[str]] = None,
                 data: Optional[List[str]] = None, **kwargs):
        self.name = name
        self.root = root
        _sources = srcs if srcs else []
        _headers = hdrs if hdrs else []
        self.sources = _sources + _headers
        _dependencies = deps if deps else []
        self.dependencies = []
        self.ext_dependencies = []
        for dep in _dependencies:
            if dep.startswith(":"):
                self.dependencies.append(f"//{root}:{dep.replace(':', '', 1)}")
            elif dep.startswith("//"):
                self.dependencies.append(dep)
            elif dep.startswith("@"):
                self.ext_dependencies.append(dep.replace("@", "", 1))

        self.selects = select if select else []
        self.data = data if data else []
        self.misc = kwargs


class cc_binary(Target):
    def __init__(self, name: str, root: str, srcs: Optional[List[str]] = None, hdrs: Optional[List[str]] = None,
                 deps: Optional[List[str]] = None, select: Optional[List[str]] = None,
                 data: Optional[List[str]] = None, **kwargs):
        super().__init__(name, root, srcs, hdrs, deps, select, data, **kwargs)


class cc_library(Target):
    def __init__(self, name: str, root: str, srcs: Optional[List[str]] = None, hdrs: Optional[List[str]] = None,
                 deps: Optional[List[str]] = None, select: Optional[List[str]] = None,
                 data: Optional[List[str]] = None, **kwargs):
        super().__init__(name, root, srcs, hdrs, deps, select, data, **kwargs)


class cc_proto(Target):
    def __init__(self, name: str, root: str, srcs: Optional[List[str]] = None, hdrs: Optional[List[str]] = None,
                 deps: Optional[List[str]] = None, select: Optional[List[str]] = None,
                 data: Optional[List[str]] = None, **kwargs):
        super().__init__(name, root, srcs, hdrs, deps, select, data, **kwargs)


class http_archive(Target):
    def __init__(self, name: str, url: Optional[str] = None, urls: Optional[List[str]] = None, **kwargs):
        super().__init__(name, "", **kwargs)
        if url:
            self.urls = [url]
        else:
            self.urls = urls


def parse_select(lines: List[str], start: int) -> Tuple[List, int]:
    global remembered_choices
    indent = len(lines[start]) - len(lines[start].lstrip())

    end = None
    selections = ["{"]
    for i, line in enumerate(lines[start + 1:]):
        if line.lstrip().startswith("})"):
            end = start + i + 1
            break
        selections.append(line)
    if not end:
        raise Exception(f"Error in parsing starting at {start}")
    selections.append("}")
    items = ast.literal_eval(''.join(selections))
    for k in items.keys():
        if k in remembered_choices:
            print(f"  Found select, using previous choice {k}")
            return items[k], end
    print("Found the following selection")
    cmap = {}
    for i, (k, v) in enumerate(items.items()):
        print(f"  {i + 1}. {k.replace('//', '')}")
        cmap[i] = k
    while True:
        ch = input("Choice: ")
        try:
            choice = int(ch) - 1
            if choice < 0 or choice >= len(items):
                raise ValueError()
            if not cmap[choice].endswith(":default"):
                remembered_choices.append(cmap[choice])
            return items[cmap[choice]], end
        except ValueError:
            print(f"Invalid selection {ch}")


def chunk(fname: str, root: str, components: Optional[List[str]] = None, max_count: Optional[int] = None) -> List[Target]:
    with open(fname, 'r') as fh:
        lines = fh.readlines()
    targets = []
    if components is None:
        components = ["cc_binary", "cc_library", "mediapipe_proto_library", "http_archive"]
    interp = {}

    c_type = ""
    # block_start = start
    inside = False
    in_select = False
    block_lines = []
    sel_end = None
    for current, line in enumerate(lines):
        if line.startswith("#"):
            continue
        if in_select:
            if current <= sel_end:
                continue
            in_select = False
        if inside:
            if line.startswith(")"):
                obj_str = "{\n" + ''.join(block_lines[1:]) + "\n}"
                obj_str = obj_str.replace("+ select", ", \n    \"select\": ")
                obj_str = ptrn.sub(r'"\1":', obj_str)
                try:
                    data = ast.literal_eval(obj_str)
                except Exception:
                    temp = obj_str.split("\n")
                    for i, t in enumerate(temp):
                        print(f"{i:2d}    {t}")
                    raise
                data['root'] = root
                if c_type == "cc_binary":
                    targets.append(cc_binary(**data))
                elif c_type == "cc_library":
                    targets.append(cc_library(**data))
                elif c_type == "cc_proto":
                    targets.append(cc_proto(**data))
                inside = False
                if len(targets) == max_count:
                    return targets
            elif "select({" in line:
                repl, sel_end = parse_select(lines, current)
                in_select = True
                loc = line.find('+')
                if loc >= 0:
                    if repl:
                        print(repl)
                        indent_count = len(lines[current - 1]) - len(lines[current - 1].lstrip())
                        indent = " " * indent_count
                        for r in repl:
                            block_lines.append(f"{indent}\"{r}\",\n")
                        # block_lines.append(str((" " * indent).join(repl)))
                    block_lines.append(line[:loc - 1] + ',\n')

                else:
                    block_lines.append(line.replace("select({\n", str(repl)) + ",\n")
            else:
                #print(interp)
                for k, v in interp.items():
                    if k in line:
                        #print(f"++++{line}")
                        l1 = line.find(' % ')
                        repl = False
                        if l1 > 0:
                            l2 = line.find(k)
                            if l2 > l1:
                                temp = line.split(' % ')
                                line = temp[0].replace("%s", v.replace('"', '')) + ",\n"
                                repl = True
                        if not repl:
                            line = line.replace(k, v)
                block_lines.append(line)
                # print(f"____{line}")

        else:
            # print(f"****{line}")
            for comp in components:
                if line.startswith(comp):
                    c_type = comp
                    inside = True
                    block_lines = [line]
                    break
            m = ptrn1.search(line)
            # print(f"     x   {m}")
            if m:
                interp[m.group(1)] = m.group(2)
                print(f"ADDING    {m.group(1)}")
                for k, v in interp.items():
                    print(f"    {k}  =  {v}")
    return targets


class BuildSystem:
    def __init__(self, name: str, root: str, path: str):
        self.name = name
        self.root = root
        self.path = path
        self.target = None
        self.externals = chunk(os.path.join(root, "WORKSPACE"), "")
        targets = chunk(os.path.join(root, path, "BUILD"), path)
        for t in targets:
            if t.name == name:
                self.target = t
                break
        if not self.target:
            raise Exception(f"Target {name} not found")
        self.components = {}

    def build(self) -> None:
        files = glob.glob(os.path.join(self.root, "**/BUILD"), recursive=True)
        temp_components = {}
        for f in files:
            base = self.root.replace("/BUILD", "")
            print(f"Parsing {f}")
            temp = chunk(f, self.root.replace(base, ""))
            for t in temp:
                name = f"//{base}:{t.name}"
                if name in temp_components:
                    continue
                temp_components[name] = t
            print(f"{f} has {len(temp)} components")
        # now parse through and find what we need


def read_bazel(component: str) -> Dict[str, Any]:
    build = BuildSystem(component, "/home/friedel/Downloads/mediapipe-0.10.14", "mediapipe/examples/desktop/hand_tracking")
    build.build()


if __name__ == '__main__':
    read_bazel("hand_tracking_cpu")
