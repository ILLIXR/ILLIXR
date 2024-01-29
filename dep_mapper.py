#!/usr/bin/env python3
"""
This module will generate a list of plugin dependencies from the header and source files of ILLIXR plugins
"""
import glob
import os
import re
import sys
from typing import List, Dict, Tuple, Set
try:
    import yaml
except Exception as _:
    print("INFO: PyYAML does not appear to be installed in your python. An updated dependency map will not be generated.")
    sys.exit(0)

lookup_re = re.compile(r".*lookup_impl<(\S+)>\(\)")  # regex for lookup_impl calls
comment_re = re.compile(r"(//.*)")                   # regex for inline comments
class_re = re.compile(r"^\s*class ([a-zA-Z0-9_]+)")  # regex for the start of a class
inh_class_re = re.compile(r"^\s*class ([a-zA-Z0-9_]+).*:.*public\s+(\S+)")  # regex for child class
class_end_re = re.compile(r"^\s*};")                 # regex for class/struct ending
struct_re = re.compile(r"^\s*struct\s+")             # regex for struct
struct2_re = re.compile(r"^\s*struct\s+.*};$")       # regex for one line struct
line_end_re = re.compile(r".*\s*;\s*(?:[^/]|//.*)$")  # regex for the end of a code line
version_re = re.compile(r"[><=]+")                   # regex for version numbers

skip_list = ['src/cxxopts.hpp']  # files to skip when scanning for classes etc.
sep = '-' * 77                   # nominal width of output tables


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


def get_classes(files: List[str]) -> Tuple[Dict[str, str], Dict[str, Dict[str, str]],
                                           Dict[str, List[str]]]:
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

    # loop over all found files (regardless of type)
    for fl in files:
        if fl in skip_list:  # skip the file if requested
            continue
        with open(fl, 'r') as fh:
            count = 0
            current = []
            in_struct = False
            # scan each line in the file
            for i, ln in enumerate(fh.readlines()):
                line = comment_re.sub('', ln)  # strip out inline comments
                cls_start = class_re.match(line)     # look for the start of a class
                # if this line starts a class, but does not contain and end-of-line character
                if cls_start and line_end_re.match(line) is None:
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
                    inh_classes[inh_cls.group(1)] = {'base': inh_cls.group(2), 'file': fl}
                lookup_res = lookup_re.match(line)  # search for calls to `lookup_impl`
                if lookup_res:
                    if lookup_res.group(1) not in lookups:
                        lookups[lookup_res.group(1)] = []
                    lookups[lookup_res.group(1)].append(fl)
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
    return classes, inh_classes, lookups


def gather(build: str) -> Tuple[Dict[str, List[Dict[str, str]]], Dict[str, Set[str]], Dict[str, str]]:
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
    plugins = glob.glob('plugins/**/plugin.cpp', recursive=True)
    hdrs += get_headers('src')
    src = get_src('src')
    ext = get_src(os.path.join(build, '_deps/audio_pipeline/src/Audio_Pipeline'), recursive=True)
    ext += get_src(os.path.join(build, '_deps/OpenVINS/src/OpenVINS'), recursive=True)

    full = hdrs + plugins + src + ext
    # extract class information from the files
    classes, inheritances, lookups = get_classes(full)

    deps = {}

    # loop over inheritance items and organize them
    for cls, data in inheritances.items():
        temp = data['file'].split(os.sep)
        file_name = temp[-1]

        # look specifically for plugins
        if temp[0] == 'plugins':
            file_name = temp[1]
        elif '_deps' in data['file']:
            file_name = temp[2]
        data['base'] = data['base'].replace('ILLIXR::', '')
        if data['base'] not in deps:
            deps[data['base']] = []
        deps[data['base']].append({'class': cls, 'from': file_name})

    invoke = {}

    # loop over lookup_impl calls and organize
    for base, used_by in lookups.items():
        for item in used_by:
            temp = item.split(os.sep)
            # look specifically for plugins
            if temp[0] == 'plugins':
                key = temp[1]
            elif '_deps' in item:
                key = temp[2]
            else:
                continue
            if key not in invoke:
                invoke[key] = set()
            invoke[key].add(base)
    return deps, invoke, classes


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
    return combined


def report_invoke(invoke: Dict[str, Set[str]]) -> None:
    """
    Prints a table of plugins and their dependencies (class names) to stdout
    :param invoke: Listing of dependencies (invoked by calls to ``lookup_impl``)
                     * **key**: plugin name
                     * **value**: listing of dependencies
    :type invoke: dict[str, set[str]]
    """
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


def report_plugin_invoke(invoke: Dict, deps: Dict) -> None:
    """
    Print a table of plugins, what plugin type they depend on, and what plugins provide that type
    to stdout.
    :param invoke:  Listing of dependencies (invoked by calls to ``lookup_impl``)
                      * **key**: plugin name
                      * **value**: listing of dependencies
    :param deps: Dictionary of parent classes and their children
                   * **key**: parent class name
                   * **value**: children class names
    :type invoke: dict[str, set[str]]
    :type deps: dict[str, list[str]]
    """
    print("\n\n\n")
    print(f"{'Plugin':25s}  {'Requires':20s}  {'Provided by':33s}")
    print(sep)
    plugins = list(invoke.keys())
    plugins.sort()
    yaml_mapper = {"dep_map": []}
    for plugin in plugins:
        uses = list(invoke[plugin])
        uses.sort()
        pl_uses = []
        for pl in uses:
            if pl in deps.keys():
                dd = []
                for x in deps[pl]:
                    dd.append(x.replace('_impl', ''))
                pl_uses.append({"needs": pl, "provided_by": dd})
        if pl_uses:
            pl_deps = []
            print(f"{plugin:25s}  {pl_uses[0]['needs']:20s} {', '.join(pl_uses[0]['provided_by'])}")
            for i in range(len(pl_uses)):
                pl_deps.append({'needs': pl_uses[i]['needs'],
                               'provided_by': pl_uses[i]['provided_by']
                                })
                if i > 0:
                    print(f"{' ':25s}  {pl_uses[i]['needs']:20s} {', '.join(pl_uses[i]['provided_by'])}")
            yaml_mapper["dep_map"].append({'plugin': plugin, 'dependencies': pl_deps})
    yaml.dump(yaml_mapper, open(os.path.join('plugins', 'plugin_deps.yaml'), 'w'))


def scan_for_find_package(flname: str) -> Dict[str, str]:
    """
    Scan the CMakeLists.txt file for `find_package` or `pkg_search_module` calls

    :param flname: The file to search.
    :type flname: str
    :return:
    Dictionary of the dependencies found and minimum version, if found.
    :rtype: dict[str, str]
    """
    packages = {}
    with open(flname, 'r') as fh:
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
                    raise Exception(f"Unexpected format {flname}  {line}")
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
                    raise Exception(f"Unexpected format {flname}  {line}")
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
    dependencies, inv, clss = gather(_build)
    comb = report_deps(dependencies)
    report_invoke(inv)
    report_plugin_invoke(inv, comb)
    process_cmake()
