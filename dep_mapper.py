#!/usr/bin/env python3

import re
import glob
import os


lookup_re = re.compile(r".*lookup_impl<(\S+)>\(\)")
class_re = re.compile(r"^\s*class ([a-zA-Z0-9_]+)")
inh_class_re = re.compile(r"^\s*class ([a-zA-Z0-9_]+).*:.*public\s+(\S+)")
class_end_re = re.compile(r"^\s*};")
struct_re = re.compile(r"^\s*struct\s+")
struct2_re = re.compile(r"^\s*struct\s+.*};$")
line_end_re = re.compile(r".*\s*;\s*(?:[^/]|//.*)$")

skiplist = ['src/cxxopts.hpp']
sep = '-' * 77


def get_headers(pth, recursive=False):
    if recursive:
        return glob.glob(f'{pth}/**/*.h', recursive=True) + glob.glob(f'{pth}/**/*.hpp', recursive=True)
    return glob.glob(f'{pth}/*.h') + glob.glob(f'{pth}/*.hpp')


def get_src(pth, recursive=False):
    if recursive:
        return glob.glob(f'{pth}/**/*.cpp', recursive=True)
    return glob.glob(f'{pth}/*.cpp')


def get_classes(files):
    classes = {}
    inh_classes = {}
    lkup = {}
    for fl in files:
        if fl in skiplist:
            continue
        with open(fl, 'r') as fh:
            count = 0
            current = []
            in_struct = False
            for i, line in enumerate(fh.readlines()):
                res = class_re.match(line)
                if res and line_end_re.match(line) is None:
                    count += 1
                    current.append(res.group(1))
                    if '::'.join(current) in classes:
                        raise Exception(f"Invalid {fl}  {i}  {line}")
                    classes['::'.join(current)] = fl
                res = inh_class_re.match(line)
                if res:
                    if res.group(1) in inh_classes:
                        raise Exception(f"Inval {fl}  {i}  {line}")
                    inh_classes[res.group(1)] = {'base': res.group(2), 'file': fl}
                res = lookup_re.match(line)
                if res:
                    if res.group(1) not in lkup:
                        lkup[res.group(1)] = []
                    lkup[res.group(1)].append(fl)
                if struct_re.match(line):
                    if struct2_re.match(line) is None:
                        in_struct = True
                if class_end_re.match(line):
                    if in_struct or count == 0:
                        in_struct = False
                        continue
                    try:
                        count -= 1
                        del current[count]
                    except IndexError as _:
                        print(f"{fl}   line {i+1}   {line}")
                        raise
            if count != 0:
                raise Exception(f"Error {fl}   {count}   {current}")
    return classes, inh_classes, lkup


def gather():
    hdrs = get_headers('include/illixr')
    plugins = glob.glob('plugins/**/plugin.cpp', recursive=True)
    hdrs += get_headers('src')
    src = get_src('src')
    ext = get_src('cmake-build-debug/_deps/audio_pipeline/src/Audio_Pipeline', recursive=True)
    ext += get_src('cmake-build-debug/_deps/OpenVINS/src/OpenVINS', recursive=True)

    full = hdrs + plugins + src + ext
    c, inh, lk = get_classes(full)

    deps = {}

    for cls, data in inh.items():
        temp = data['file'].split(os.sep)
        fname = temp[-1]

        if temp[0] == 'plugins':
            fname = temp[1]
        elif '_deps' in data['file']:
            fname = temp[2]
        data['base'] = data['base'].replace('ILLIXR::', '')
        if data['base'] not in deps:
            deps[data['base']] = []
        deps[data['base']].append({'class': cls, 'from': fname})

    invoke = {}
    for base, used_by in lk.items():
        for item in used_by:
            temp = item.split(os.sep)
            if temp[0] == 'plugins':
                key = temp[1]
            elif '_deps' in item:
                key = temp[2]
            else:
                continue
            if key not in invoke:
                invoke[key] = set()
            invoke[key].add(base)

    return deps, invoke


def report_deps(deps):
    print(f"{'Base class':25s}  {'Child class':50s}")
    print(sep)
    bases = list(deps.keys())
    bases.sort()
    for base in bases:
        data = []
        for i in deps[base]:
            data.append(i['class'])
        data.sort()
        line = f"{base:25s}  "
        temp = ', '.join(data)
        if len(temp) > 50:
            start = 0
            idx = 1
            for i in range(1, len(data)):
                if len(', '.join(data[start: i])) < 50:
                    idx = i
                else:
                    line += ', '.join(data[start: idx]) + ','
                    print(line)
                    start = idx
                    line = f"{' ':25s}  "
            line += ', '.join(data[start:])
        else:
            line += temp
        print(line)


def report_invoke(invoke):
    print("\n\n\n")
    print(f"{'Plugin':25s}  {'Requires':50s}")
    print(sep)
    plugins = list(invoke.keys())
    plugins.sort()
    for plugin in plugins:
        uses = list(invoke[plugin])
        uses.sort()
        line = f"{plugin:25s}  "
        temp = ', '.join(uses)
        if len(temp) > 50:
            start = 0
            idx = 1
            for i in range(1, len(uses)):
                if len(', '.join(uses[start: i])) < 50:
                    idx = i
                else:
                    line += ', '.join(uses[start: idx])
                    print(line)
                    start = idx
                    line = f"{' ':25s}  "
            line += ', '.join(uses[start:])
        else:
            line += temp
        print(line)


if __name__ == '__main__':
    dependencies, inv = gather()
    report_deps(dependencies)
    report_invoke(inv)
