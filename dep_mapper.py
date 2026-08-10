#!/usr/bin/env python3
"""
This module will generate a list of plugin dependencies from the header and source files of ILLIXR plugins
"""
from copy import deepcopy
import glob
import os
import re
import sys
from typing import List, Dict, Set, Any

try:
    import yaml
except Exception as _:
    print("INFO: PyYAML does not appear to be installed in your python. An updated dependency map will not be generated.")
    sys.exit(0)

alias_re = re.compile(r"namespace (\S+)\s?=\s?(\S+);")
build_re = re.compile(r"\S*build/_deps/([a-zA-Z0-9_]*)/\S+\.[ch]pp")
class_end_re = re.compile(r"^\s*};")  # regex for class/struct ending
class_re = re.compile(r"^\s*class (?:MY_EXPORT_API )?([a-zA-Z0-9_]+)")  # regex for the start of a class
comment_re = re.compile(r"(//.*)")  # regex for inline comments
inh_class_re = re.compile(r"^\s*class (?:MY_EXPORT_API )?([a-zA-Z0-9_]+).*:.*public.*")  # regex for child class
inh_class_sub_re = re.compile(r"public\s+([a-zA-Z0-9_:]+)")
# inh_class_ml_re = re.compile(r"^\s*[:,].*public\s+([a-zA-Z0-9_:]+)\s*{?")  # regex for child class with multiline definition
line_end_re = re.compile(r".*\s*;\s*(?:[^/]|//.*)$")  # regex for the end of a code line
lookup_re = re.compile(r".*lookup_impl<(\S+)>\(\)")  # regex for lookup_impl calls
plugin_re = re.compile(r"\S*plugins/(\S+)/\S+\.[ch]pp")
reader_re = re.compile(r".*->get(?:_buffered)?_reader<(?:ILLIXR::)?(?:data_format::)?(\S+)>\(\"(\S+)\"\).*")
schedule_re = re.compile(r"\s*\S+->schedule<(\S+)>\s?\(\s*\S+\s*,\s*\"(\S+)\".*")
service_re = re.compile(r"\S*services/(\S+)/\S+\.[ch]pp")
struct_re = re.compile(r"^\s*struct\s+")  # regex for struct
struct2_re = re.compile(r"^\s*struct\s+.*};$")  # regex for one line struct
writer_re = re.compile(r".*->get(?:_network)?_writer<(?:ILLIXR::)?(?:data_format::)?(\S+)>\(\"(\S+)\"\).*")
version_re = re.compile(r"[><=]+")  # regex for version numbers

skip_list = ['src/cxxopts.hpp',
             "plugins/zed/capture/cxxopts.hpp",
             "plugins/offload_vio/proto/output_stub.hpp",
             "plugins/offload_vio/proto/input_stub.hpp",
             "plugins/ada/proto/input_stub.hpp",
             "plugins/ada/proto/output_stub.hpp"]  # files to skip when scanning for classes etc.
sep = '-' * 77                   # nominal width of output tables


def wrap_line(items: List[str], indent: int = 0, length: int = 50) -> str:
    """
    Generate lines of word wrapped text from an input list of items
    :param items: the line to wrap
    :param indent: the number of spaces to indent subsequent lines
    :param length: the max number of characters per line
    :return: the word wrapped text
    """
    out_line = ', '.join(items)
    if len(out_line) > length:
        out_line = ""
        line = ""
        start = 0
        idx = 1
        for i in range(1, len(items)):
            if len(', '.join(items[start: i])) < length:
                idx = i
            else:
                line += ', '.join(items[start: idx]) + ',\n'
                out_line += line
                start = idx
                line = ' ' * indent + "  "
        line += ', '.join(items[start:])
        out_line += line
    return out_line


def search_for_files(path: str, extensions: List[str], recursive: bool = False) -> List[str]:
    """
    Searches the input path for header files and returns a list of what is found.

    :param path: The directory to search for files of the specified type in, relative paths work, absolute may not.
    :param extensions: List of extensions (with no preceding ``.``) to search for.
    :param recursive: If ``True``, then search recursively within ``path`` for files. Default is
                      ``False``, meaning no recursive search.
    :type path: str
    :type extensions: list[str]
    :type recursive: bool
    :return: A list of the files found.
    :rtype: list[str]
    """
    found_files = []
    if recursive:  # if recursive add extra path specifier
        path = os.path.join(path, '**')
    for ext in extensions:  # loop over the given extensions
        found_files += glob.glob(os.path.join(path, f'*.{ext}'), recursive=recursive)
    return found_files


def get_headers(path: str, recursive: bool = False) -> List[str]:
    """
    Searches the input path for header files and returns a list of what is found.

    :param path: The directory to search for header files in, relative paths work, absolute may not.
    :param recursive: If ``True``, then search recursively within ``path`` for header files. Default is
                      ``False``, meaning no recursive search.
    :type path: str
    :type recursive: bool
    :return: A list of the header files found.
    :rtype: list[str]
    """
    return search_for_files(path, ['h', 'hpp'], recursive)


def get_src(path: str, recursive: bool = False) -> List[str]:
    """
    Searches the input path for C++ source files and returns a list of what is found.

    :param path: The directory to search for source files in, relative paths work, absolute may not.
    :param recursive: If ``True``, then search recursively within ``path`` for source files. Default is
                      ``False``, meaning no recursive search.
    :type path: str
    :type recursive: bool
    :return: A list of the C++ source files found.
    :rtype: list[str]
    """
    return search_for_files(path, ['cpp'], recursive)


def get_plugin_name(file_name: str) -> str:
    """
    Determines the plugin name from the input file name

    :param file_name: the file name
    :return: the plugin name
    """
    if 'plugins' in file_name:
        p_match = plugin_re.match(file_name)
    elif 'services' in file_name:
        p_match = service_re.match(file_name)
    elif 'build' in file_name and '_deps' in file_name:
        p_match = build_re.match(file_name)
    else:
        raise Exception(f"Unknown type {file_name}")
    if not p_match:
        raise Exception(f"Cannot determine plugin name from {file_name}")
    return p_match.group(1).lower().replace("/", ".")

def get_classes(files: List[str]) -> tuple[
    dict[str, str], dict[str, list[Any]], dict[str, list[Any]], list[str], dict[str, list[str]], dict[str, list[str]]]:
    """
    Searches the input list of files for ``class`` definitions to track inheritance and use of other
    plugins.
    :param files: List of files to search for class information in.
    :type files: list[str]
    :return: A tuple of three dictionaries:
             * Listing of all classes found (including inner classes)
                 * **key**: class name
                 * **value**: file with class definition, including relative path
             * Listing of class inheritance
                 * **key**: class name
                 * **value**: dictionary
                     * **key**: parent class name
                     * **value**: file with class definition, including relative path
             * Listing of calls to ``lookup_impl``
                 * **key**: the class being loaded by the ``lookup_impl`` call
                 * **value**: list of the files loading the class
    :rtype: tuple[dict, dict, dict]
    """
    classes = {}
    inh_classes = {}
    lookups = {}
    readers = {}
    writers = {}
    system_classes = ['relative_clock']
    # loop over all found files (regardless of type)
    for fl in files:
        if fl in skip_list:  # skip the file if requested
            continue
        with open(fl, 'r') as fh:
            count = 0
            current = []
            aliases = {}
            in_struct = False
            in_class_def = False
            current_def = None
            # scan each line in the file
            for i, ln in enumerate(fh.readlines()):
                line = comment_re.sub('', ln)  # strip out inline comments
                temp = line.strip()
                if temp.startswith('* '):   # skip multi line comments
                    continue
                alias_sr = alias_re.match(line)
                if alias_sr:
                    aliases[alias_sr.group(1)] = alias_sr.group(2)
                if in_class_def:
                    in_class_def = False
                    parents = inh_class_sub_re.findall(line)
                    for p in parents:
                        inh_classes[current_def].append({'base': p, 'file': fl})
                    if line.find("{") < 0:
                        in_class_def = True
                        continue
                cls_start = class_re.match(line)     # look for the start of a class
                # if this line starts a class, but does not contain and end-of-line character
                if cls_start and line_end_re.match(line) is None:
                    if fl.startswith("src/") or fl.startswith("include/"):
                        system_classes.append(cls_start.group(1))
                    count += 1
                    # track the current class name (even for inner classes)
                    current.append(cls_start.group(1))
                    # determine if this is an inner class
                    if '::'.join(current) in classes:
                        raise Exception(f"Cannot handle this format {fl}  {i}  {line}")
                    classes['::'.join(current)] = fl
                inh_cls = inh_class_re.match(line)  # look for class inheritance
                if inh_cls:
                    if inh_cls.group(1) in inh_classes:
                        raise Exception(f"Unexpected format {fl}  {i}  {line}")
                    inh_classes[inh_cls.group(1)] = []
                    current_def = inh_cls.group(1)
                    parents = inh_class_sub_re.findall(line)
                    for p in parents:
                        inh_classes[inh_cls.group(1)].append({'base': p, 'file': fl})
                    if line.find("{") < 0:
                        in_class_def = True
                        continue
                    # inh_classes[inh_cls.group(1)] = {'base': inh_cls.group(2), 'file': fl}
                lookup_res = lookup_re.match(line)  # search for calls to `lookup_impl`
                if lookup_res:
                    if lookup_res.group(1) not in lookups:
                        lookups[lookup_res.group(1)] = []
                    lookups[lookup_res.group(1)].append(fl)
                reader_res = reader_re.match(line)
                if reader_res:
                    if "event_wrapper" not in reader_res.group(1):
                        plug = get_plugin_name(fl)
                        if plug not in readers:
                            readers[plug] = []
                        readers[plug].append(reader_res.group(1).replace('ILLIXR::', '').replace('data_format::', ''))
                writer_res = writer_re.match(line)
                if writer_res:
                    if "event_wrapper" not in writer_res.group(1):
                        plug = get_plugin_name(fl)
                        if plug not in writers:
                            writers[plug] = []
                        writers[plug].append(writer_res.group(1).replace('ILLIXR::', '').replace('data_format::', ''))
                sched = schedule_re.match(line)
                if sched:
                    item = sched.group(1)
                    plug = get_plugin_name(fl)
                    for k, v in aliases.items():
                        if k + "::" in item:
                            item = item.replace(k + "::", v + "::")
                    if plug not in readers:
                        readers[plug] = []
                    readers[plug].append(item.replace('ILLIXR::', '').replace('data_format::', ''))
                # detect the start of a struct definition, this is only used to accurately track class
                # definitions since structs and classes end in the same fashion
                if struct_re.match(line):
                    # if this is not a one line struct
                    if struct2_re.match(line) is None:
                        in_struct = True
                # determine if the line is the end of a class or struct
                if class_end_re.match(line):
                    if in_struct or count == 0:
                        in_struct = False
                        continue
                    try:
                        count -= 1
                        del current[count]
                    except IndexError as _:
                        print(f"Mismatched class count {fl}   line {i+1}   {line}")
                        raise
            # if we think we are still in a class, but are at the end of the file
            if count != 0:
                raise Exception(f"Error {fl}   {count}   {current}")
    return classes, inh_classes, lookups, system_classes, readers, writers


def gather(build: str) -> tuple[
    dict[Any, list[Any]], dict[Any, set[Any]], dict[str, str], list[str], dict[str, list[str]], dict[str, list[str]]]:
    """
    Generate mappings of plugin class inheritance, plugin requirements, and plugin interdependencies
    :param build: The build directory
    :type build: str
    :return: A tuple of dictionaries:
             * Listing of class inheritances
                 * **key**: parent class name
                 * **value**: List of dictionaries
                     * **key**: child class name
                     * **value**: file containing the class definition, including relative path
             * Listing of dependencies (invoked by calls to ``lookup_impl``)
                 * **key**: plugin name
                 * **value**: listing of dependencies
             * Listing of all classes found (including inner classes)
                 * **key**: class name
                 * **value**: file with class definition, including relative path
    :rtype: tuple[dict, dict, dict]
    """
    # get initial file listings
    hdrs = get_headers('include/illixr')
    hdrs += get_headers('include/illixr/vk_util')
    plugins = glob.glob('plugins/**/*.cpp', recursive=True)
    hdrs += get_headers('plugins', True)
    hdrs += get_headers('src')
    src = get_src('src')
    ext = get_src(os.path.join(build, '_deps/audio_pipeline/src/Audio_Pipeline'), recursive=True)
    ext += get_src(os.path.join(build, '_deps/OpenVINS/src/OpenVINS'), recursive=True)
    ext += get_src(os.path.join(build, '_deps/hand_tracking/src/HAND_TRACKING'), recursive=True)

    full = hdrs + plugins + src + ext
    # extract class information from the files
    classes, inheritances, lookups, system_classes, readers, writers = get_classes(full)

    deps = {}

    # loop over inheritance items and organize them
    for cls, data in inheritances.items():
        for d in data:
            temp = d['file'].split(os.sep)
            file_name = temp[-1]

            # look specifically for plugins
            if temp[0] == 'plugins':
                file_name = temp[1]
            elif '_deps' in d['file']:
                file_name = temp[2]
            d['base'] = d['base'].replace('ILLIXR::', '').replace('data_format::', '')
            if d['base'] not in deps:
                deps[d['base']] = []
            deps[d['base']].append({'class': cls, 'from': file_name})

    invoke = {}

    # loop over lookup_impl calls and organize
    for base, used_by in lookups.items():
        for item in used_by:
            try:
                key = get_plugin_name(item)
            except:
                continue
            if key not in invoke:
                invoke[key] = set()
            invoke[key].add(base)
    return deps, invoke, classes, system_classes, readers, writers


def report_deps(deps: Dict[str, List[Dict[str, str]]]) -> Dict:
    """
    Print a table of base classes and their child classes to stdout.

    :param deps: Listing of class inheritances
                   * **key**: parent class name
                   * **value**: List of dictionaries
                     * **key**: child class name
                     * **value**: file containing the class definition, including relative path
    :type deps: dict[str, list[dict[str, str]]]
    :return: Dictionary of parent classes and their children
               * **key**: parent class name
               * **value**: children class names
    :rtype: dict[str, list[str]]
    """
    print(f"{'Base class':25s}  {'Child class':50s}")
    print(sep)
    bases = list(deps.keys())
    bases.sort()
    combined = {}
    for base in bases:
        data = []
        for i in deps[base]:
            data.append(i['class'])
        data.sort()
        line = f"{base:25s}  "
        combined[base] = data
        line += wrap_line(data, 25, 50)
        print(line)
    return combined


def report_invoke(invoke: Dict[str, Set[str]], readers: Dict[str, List[str]]) -> None:
    """
    Prints a table of plugins and their dependencies (class names) to stdout
    :param invoke: Listing of dependencies (invoked by calls to ``lookup_impl``)
                     * **key**: plugin name
                     * **value**: listing of dependencies
    :param readers: Listing of dependencies (invoked by calls to ``get_reader`` or ``get_buffered_reader``)
                     * **key**: plugin name
                     * **value**: listing of dependencies
    :type invoke: dict[str, set[str]]
    :type readers: dict[str, list[str]]
    """
    print("\n\n\n")
    print(f"{'Plugin':25s}  {'Requires':50s}")
    print(sep)
    plugins = list(invoke.keys())
    plugins.sort()
    for plugin in plugins:
        uses = list(invoke[plugin])
        if plugin in readers:
            uses += readers[plugin]
        uses.sort()
        line = f"{plugin:25s}  "
        line += wrap_line(uses, 25, 50)
        print(line)


def report_writers(writers: Dict[str, List[str]]) -> None:
    """
    Report which plugins provide which items
    :param writers: listing of plugins and what items they write
                     * **key**: plugin name
                     * **value**: listing of items written
    :type writers: dict[str, list[str]]
    """
    print("\n\n\n")
    print(f"{'Plugin':25s}  {'Provides':50s}")
    print(sep)
    plugins = list(writers.keys())
    plugins.sort()
    for plugin in plugins:
        provides = writers[plugin]
        provides.sort()
        line = f"{plugin:25s}  "
        line += wrap_line(provides, 25, 50)

        print(line)

def report_plugin_invoke(invoke: Dict, deps: Dict, system_classes: List, readers: Dict[str, List[str]], writers: Dict[str, List[str]]) -> None:
    """
    Print a table of plugins, what plugin type they depend on, and what plugins provide that type
    to stdout.
    :param invoke:  Listing of dependencies (invoked by calls to ``lookup_impl``)
                      * **key**: plugin name
                      * **value**: listing of dependencies
    :param deps: Dictionary of parent classes and their children
                   * **key**: parent class name
                   * **value**: children class names
    :param system_classes: listing of system classes
    :param readers: listing of plugins and their readers
    :param writers: listing of plugins and their writers
    :type invoke: dict[str, set[str]]
    :type deps: dict[str, list[str]]
    """
    print("\n\n\n")
    print(f"{'Plugin':25s}  {'Requires':20s}  {'Provided by':33s}")
    print(sep)
    plugins = list(invoke.keys())
    plugins.sort()
    providers = {}
    for plugin, writers in writers.items():
        for writer in writers:
            if writer not in providers:
                providers[writer] = []
            providers[writer].append(plugin)
    yaml_mapper = {"dep_map": []}
    for plugin in plugins:
        uses = list(invoke[plugin])
        if plugin in readers:
            uses += readers[plugin]
        uses.sort()
        pl_uses = []
        for pl in uses:
            if pl in system_classes:
                continue
            if pl in deps.keys():
                dd = []
                for x in deps[pl]:
                    if x not in system_classes:
                        dd.append(x.replace('_impl', ''))
                if dd:
                    pl_uses.append({"needs": pl, "provided_by": dd})
            elif pl in providers.keys():
                prov_by = providers[pl]
                prov_by.sort()
                if plugin not in prov_by:
                    pl_uses.append({'needs': pl, "provided_by": prov_by})
            else:
                print(f"Cannot find something that provides {pl}")
        if pl_uses:
            pl_deps = []
            print(f"{plugin:25s}  {pl_uses[0]['needs']:20s} {', '.join(pl_uses[0]['provided_by'])}")
            for i in range(len(pl_uses)):
                pl_deps.append({'needs': pl_uses[i]['needs'],
                                'provided_by': pl_uses[i]['provided_by']
                                })
                if i > 0:
                    print(f"{' ':25s}  {pl_uses[i]['needs']:20s} {', '.join(pl_uses[i]['provided_by'])}")
            yaml_mapper["dep_map"].append({'plugin': plugin, 'dependencies': deepcopy(pl_deps)})
    yaml.dump(yaml_mapper, open(os.path.join('plugins', 'plugin_deps.yaml'), 'w'))


def scan_for_find_package(file_name: str) -> Dict[str, str]:
    """
    Scan the CMakeLists.txt file for `find_package` or `pkg_search_module` calls

    :param file_name: The file to search.
    :type file_name: str
    :return:
    Dictionary of the dependencies found and minimum version, if found.
    :rtype: dict[str, str]
    """
    packages = {}
    with open(file_name, 'r') as fh:
        for ln in fh.readlines():
            line = ln.strip()
            if line.startswith('find_package'):
                start = line.find('(')
                end = line.find(')')
                temp = line[start+1:end].split()
                if len(temp) == 1:
                    packages[temp[0]] = None
                elif len(temp) == 2:
                    if temp[1] in ['REQUIRED', 'QUIET']:
                        packages[temp[0]] = None
                    else:
                        packages[temp[0]] = temp[1]
                elif len(temp) >= 3:
                    if temp[1].startswith('$'):
                        packages[temp[0]] = '*'
                    elif temp[1] in ['REQUIRED', 'QUIET', 'COMPONENTS']:
                        packages[temp[0]] = None
                    else:
                        packages[temp[0]] = temp[1]
                else:
                    raise Exception(f"Unexpected format {file_name}  {line}")
            elif line.startswith('pkg_search_module') or line.startswith('pkg_check_modules'):
                start = line.find('(')
                end = line.find(')')
                temp = line[start+1:end].split()[-1]
                temp = version_re.sub(' ', temp)
                temp = temp.split()
                if len(temp) == 1:
                    packages[temp[0]] = None
                elif len(temp) == 2:
                    packages[temp[0]] = temp[1]
                else:
                    raise Exception(f"Unexpected format {file_name}  {line}")
            elif line.startswith('get_external'):
                start = line.find('(')
                end = line.find(')')
                temp = line[start+1:end]
                packages[temp] = None
    return packages


def scan_for_cmake_files() -> Dict[str, List[str]]:
    """
    Scan the directories for cmake files and return a list of what is found.

    :return: A list of the cmake files found, including the path.
    :rtype: list[str]
    """
    path = os.path.dirname(os.path.abspath(__file__))
    cm_files = glob.glob(os.path.join(path, 'cmake', 'Get*.cmake'), recursive=False)
    files = glob.glob(os.path.join(path, 'plugins', '**', 'CMakeLists.txt'), recursive=True)
    files.append(os.path.join(path, 'CMakeLists.txt'))
    files.append(os.path.join(path, 'src', 'CMakeLists.txt'))
    return {"cmake": cm_files, "CMakeLists.txt": files}


def report_ext_deps(deps: Dict[str, Dict[str, str]]) -> None:
    """
    Print a table of external dependencies to docs.
    :param deps: Dependency mapping
    :type deps: dict[str, dict[str, str]]
    """
    path = os.path.dirname(os.path.abspath(__file__))
    with open(os.path.join(path, 'docs', 'docs', 'external_dependencies.rst'), 'w') as fh:
        fh.write("External Dependencies\n")
        fh.write("=====================\n\n")
        fh.write("| Plugin | Dependency | Version |\n")
        fh.write("| ------ | ---------- | ------- |\n")
        for pkg, data in deps.items():
            keys = list(data.keys())
            keys.sort()
            vers_line = ""
            depline = ""
            for key in keys:
                depline += f"{key}<br>"
                if data[key] is None:
                    vers_line += "<br>"
                else:
                    vers_line += f"{data[key]}<br>"
            fh.write(f"| {pkg} | {depline} | {vers_line} |\n")


def report_rev_deps(deps: Dict[str, List[str]]) -> None:
    """
    Print a table of reverse dependencies to docs.
    :param deps: Dependency mapping
    :type deps: dict[str, list[str]]
    """
    path = os.path.dirname(os.path.abspath(__file__))
    with open(os.path.join(path, 'docs', 'docs', 'reverse_dependencies.rst'), 'w') as fh:
        fh.write("Reverse Dependencies\n")
        fh.write("====================\n\n")
        fh.write("| Dependency | Plugins |\n")
        fh.write("| ---------- | ------- |\n")
        for pkg, data in deps.items():
            data.sort()
            fh.write(f"| {pkg} | {'<br> '.join(data)} |\n")


def process_cmake() -> None:
    """
    Process the cmake files to generate a dependency map.
    """
    files = scan_for_cmake_files()
    deps = {}
    second_deps = {}
    reverse_deps = {}
    for fl in files['cmake']:
        pkg = fl.split('/')[-1].replace('.cmake', '').replace('Get', '')
        second_deps[pkg] = scan_for_find_package(fl)
    for fl in files['CMakeLists.txt']:
        temp = fl.split('/')
        if temp[-3] == 'plugins':
            pkg = temp[-2]
        elif temp[-2] == 'src':
            pkg = 'main'
        else:
            pkg = 'global'
        deps[pkg] = scan_for_find_package(fl)
        if len(deps[pkg]) == 0:
            del deps[pkg]
    for pkg, data in deps.items():
        keys = list(data.keys())
        for key in keys:
            if key in second_deps:
                data.update(second_deps[key])
            if key not in reverse_deps:
                reverse_deps[key] = []
            reverse_deps[key].append(pkg)
    report_ext_deps(deps)
    report_rev_deps(reverse_deps)


if __name__ == '__main__':
    if len(sys.argv) != 2:
        raise Exception("Incorrect number of arguments.")
    _build = sys.argv[1]
    dependencies, inv, clss, sys_cls, readers_, writers_ = gather(_build)
    comb = report_deps(dependencies)
    report_invoke(inv, readers_)
    report_writers(writers_)
    report_plugin_invoke(inv, comb, sys_cls, readers_, writers_)
    process_cmake()
