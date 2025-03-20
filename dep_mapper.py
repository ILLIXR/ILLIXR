#!/usr/bin/env python3
"""
This module will generate a list of plugin dependencies from the header and source files of ILLIXR plugins
"""
from copy import deepcopy
import glob
import os
import re
import sys
from typing import List, Dict, Set, Any, Union, Optional

try:
    import yaml
except Exception as _:
    print(
        "INFO: PyYAML does not appear to be installed in your python. An updated dependency map will not be generated.")
    sys.exit(0)

alias_re = re.compile(r"namespace (\S+)\s?=\s?(\S+);")
build_re = re.compile(r"\S*build/_deps/([a-zA-Z0-9_]*)/\S+\.[ch]pp")
class_end_re = re.compile(r"^\s*};")  # regex for class/struct ending
class_re = re.compile(r"^\s*class ([a-zA-Z0-9_]+)")  # regex for the start of a class
comment_re = re.compile(r"(//.*)")  # regex for inline comments
inh_class_re = re.compile(r"^\s*class ([a-zA-Z0-9_]+).*:.*public.*")  # regex for child class
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
             "plugins/offload_vio/proto/input_stub.hpp"]  # files to skip when scanning for classes etc.
sep = '-' * 77  # nominal width of output tables

service_names = ['pose_prediction', 'vulkan::timewarp', 'vulkan::app']


class NoAliasDumper(yaml.Dumper):
    def ignore_aliases(self, data):
        return True


class Lookup:
    def __init__(self, dtype: str, writer: Optional[str] = None, reader: Optional[str] = None):
        self.type: str = dtype
        self.name = None
        self.writers: Set = set()
        self.readers: Set = set()
        if writer:
            self.writers.add(writer)
        if reader:
            self.readers.add(reader)

    def add_writer(self, writer: str):
        self.writers.add(writer)

    def add_reader(self, reader: str):
        self.readers.add(reader)

    def __str__(self):
        output = f"{self.type}:\n"
        if self.writers:
            output += "  writers:\n"
            for i in self.writers:
                output += f"    {i}\n"
        if self.readers:
            output += "  readers:\n"
            for i in self.readers:
                output += f"    {i}\n"
        return output

    def __repr__(self):
        return str(self)


class Lookups(list):
    def __init__(self):
        super().__init__()

    def append(self, item: Lookup):
        have = False
        for t in self:
            if item.type == t.type and item.name == t.name:
                t.add_provider(item.readers)
                have = True
                break
        if not have:
            super().append(item)

    def update(self, items: List[Lookup]):
        for item in items:
            self.append(item)

    def __setitem__(self, key, value):
        raise RuntimeError("Cannot directly set an Item.")

    def __contains__(self, dtype: Union[str, Lookup], name: Optional[str] = None) -> bool:
        if isinstance(dtype, Lookup):
            return super().__contains__(dtype)
        for item in self:
            if dtype == item.type and name == item.name:
                return True
        return False

    def l_index(self, dtype: str, name: Optional[str] = None) -> int:
        for i, item in enumerate(self):
            if dtype == item.type and name == item.name:
                return i
        return -1

    def __getitem__(self, item: Union[str, int]):
        if isinstance(item, int):
            return super().__getitem__(item)
        idx = self.l_index(item)
        if idx >= 0:
            return super().__getitem__(idx)
        raise RuntimeError("Invalid item")

    def __str__(self):
        output = ""
        for i in self:
            output += str(i)
        return output

    def __repr__(self):
        return str(self)


class Topic(Lookup):
    def __init__(self, dtype: str, name: str, writer: Optional[str] = None, reader: Optional[str] = None,
                 async_reader: Optional[str] = None):
        super().__init__(dtype, writer, reader)
        self.name = name
        self.async_readers: Set = set()

        if async_reader:
            self.async_readers.add(async_reader)

    def add_async_reader(self, reader: str):
        self.async_readers.add(reader)

    def __str__(self):
        if not self.name:
            return ""
        output = f"{self.name} <{self.type}>:\n"
        if self.writers:
            output += "  writers:\n"
            for i in self.writers:
                output += f"    {i}\n"
        if self.readers:
            output += "  readers:\n"
            for i in self.readers:
                output += f"    {i}\n"
        if self.async_readers:
            output += "  async_readers:\n"
            for i in self.async_readers:
                output += f"    {i}\n"
        return output


class Topics(Lookups):
    def __init__(self):
        super().__init__()


class Plugin:
    def __init__(self, name: str, topics_: Topics, lookups_: Lookups, writers: Union[List[Topic], Topic, None] = None,
                 readers: Union[List[Topic], Topic, None] = None,
                 async_readers: Union[List[Lookup], Lookup, None] = None,
                 lookups: Union[List[Topic], Topic, None] = None, is_svc: bool = False):
        self.name = name
        self.topics: Topics = topics_
        self.lookups: Lookups = lookups_
        if isinstance(writers, Topic):
            self.writers = [writers]
        elif isinstance(writers, list):
            self.writers = writers
        else:
            self.writers = []
        if isinstance(readers, Topic):
            self.readers = [readers]
        elif isinstance(readers, list):
            self.readers = readers
        else:
            self.readers = []
        if isinstance(async_readers, Topic):
            self.async_readers = [async_readers]
        elif isinstance(async_readers, list):
            self.async_readers = async_readers
        else:
            self.async_readers = []
        if isinstance(lookups, Lookup):
            self.l_ups = [lookups]
        elif isinstance(lookups, list):
            self.l_ups = lookups
        else:
            self.l_ups = []
        self.is_service = is_svc

    def add_reader(self, dtype: str, name: str):
        idx = self.topics.l_index(dtype, name)
        if idx >= 0:
            self.topics[idx].add_reader(self.name)
            self.readers.append(self.topics[idx])
        else:
            t = Topic(dtype, name, reader=self.name)
            self.topics.append(t)
            self.readers.append(t)

    def add_async_reader(self, dtype: str, name: str):
        idx = self.topics.l_index(dtype, name)
        if idx >= 0:
            self.topics[idx].add_async_reader(self.name)
            self.async_readers.append(self.topics[idx])
        else:
            t = Topic(dtype, name, async_reader=self.name)
            self.topics.append(t)
            self.async_readers.append(t)

    def add_writer(self, dtype: str, name: str):
        idx = self.topics.l_index(dtype, name)
        if idx >= 0:
            self.topics[idx].add_writer(self.name)
            self.writers.append(self.topics[idx])
        else:
            t = Topic(dtype, name, writer=self.name)
            self.topics.append(t)
            self.writers.append(t)

    def add_lookup(self, dtype: str):
        idx = self.lookups.l_index(dtype)
        if idx >= 0:
            self.lookups[idx].add_reader(self.name)
            self.readers.append(self.lookups[idx])
        else:
            t = Lookup(dtype, reader=self.name)
            self.lookups.append(t)
            self.readers.append(t)

    def __str__(self):
        output = f"\n{self.name}:\n"
        if self.readers:
            output += "  reads:\n"
            for r in self.readers:
                output += f"    {r.name} <{r.type}>\n"
        if self.async_readers:
            output += "  async_reads:\n"
            for r in self.async_readers:
                output += f"    {r.name} <{r.type}>\n"
        if self.writers:
            output += "  writes:\n"
            for r in self.writers:
                output += f"    {r.name} <{r.type}>\n"
        return output

    def __repr__(self):
        return str(self)


class Inherit:
    def __init__(self, base: str, fl: str):
        self.base = base
        self.file = fl

    def __str__(self):
        return f"{self.base} from  {self.file}\n"

    def __repr__(self):
        return str(self)


class Inheritors:
    def __init__(self, name: str):
        self.name = name
        self.data: List[Inherit] = []

    def append(self, item: Inherit):
        self.data.append(item)

    def __iter__(self):
        return self.data.__iter__()

    def __contains__(self, item):
        return self.data.__contains__(item)

    def __str__(self):
        output = ""
        for inh in self.data:
            output += str(inh)
        return output

    def __repr__(self):
        return str(self)


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


def replace_namespace(item: str) -> str:
    return item.replace('ILLIXR::', '').replace('data_format::', '')


def get_classes(files: List[str]) -> tuple[
    dict[str, Inheritors], Lookups, list[str], dict[str, Plugin], Topics]:
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
    inh_classes = {}
    lookups = Lookups()
    plugins = {}
    topics_ = Topics()
    system_classes = ['relative_clock', 'switchboard', 'vulkan::display_provider', 'xlib_gl_extended_window']
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
                if temp.startswith('* '):  # skip multi line comments
                    continue
                alias_sr = alias_re.match(line)
                if alias_sr:
                    aliases[alias_sr.group(1)] = alias_sr.group(2)
                if in_class_def:
                    in_class_def = False
                    parents = inh_class_sub_re.findall(line)
                    for p in parents:
                        inh_classes[current_def].append(Inherit(replace_namespace(p), fl))
                    if line.find("{") < 0:
                        in_class_def = True
                        continue
                cls_start = class_re.match(line)  # look for the start of a class
                # if this line starts a class, but does not contain and end-of-line character
                if cls_start and line_end_re.match(line) is None:
                    if fl.startswith("src/") or fl.startswith("include/"):
                        system_classes.append(cls_start.group(1))
                    count += 1
                    # track the current class name (even for inner classes)
                    current.append(cls_start.group(1))
                    # determine if this is an inner class
                inh_cls = inh_class_re.match(line)  # look for class inheritance
                if inh_cls:
                    for svc in service_names:
                        if svc in line:
                            plug = get_plugin_name(fl)
                            if plug not in plugins:
                                plugins[plug] = Plugin(plug, topics_, lookups, is_svc=True)
                            plugins[plug].add_writer(svc, "")

                    inh_name = inh_cls.group(1).replace('_impl', '')
                    if inh_name not in inh_classes:
                        inh_classes[inh_name] = Inheritors(inh_name)
                    current_def = inh_name
                    parents = inh_class_sub_re.findall(line)
                    for p in parents:
                        inh_classes[inh_name].append(Inherit(replace_namespace(p), fl))
                    if line.find("{") < 0:
                        in_class_def = True
                        continue
                    # inh_classes[inh_name] = {'base': inh_cls.group(2), 'file': fl}
                lookup_res = lookup_re.match(line)  # search for calls to `lookup_impl`
                if lookup_res:
                    if fl.startswith("plugins/") or fl.startswith("services/"):
                        plug = get_plugin_name(fl)
                        if plug not in plugins:
                            plugins[plug] = Plugin(plug, topics_, lookups)
                        l_name = replace_namespace(lookup_res.group(1))
                        if l_name not in system_classes:
                            plugins[plug].add_lookup(l_name)

                reader_res = reader_re.match(line)
                if reader_res:
                    if "event_wrapper" not in reader_res.group(1):
                        plug = get_plugin_name(fl)
                        if plug not in plugins:
                            plugins[plug] = Plugin(plug, topics_, lookups)
                        plugins[plug].add_reader(replace_namespace(reader_res.group(1)), reader_res.group(2))
                writer_res = writer_re.match(line)
                if writer_res:
                    if "event_wrapper" not in writer_res.group(1):
                        plug = get_plugin_name(fl)
                        if plug not in plugins:
                            plugins[plug] = Plugin(plug, topics_, lookups)
                        plugins[plug].add_writer(replace_namespace(writer_res.group(1)), writer_res.group(2))
                sched = schedule_re.match(line)
                if sched:
                    item = sched.group(1)
                    plug = get_plugin_name(fl)
                    for k, v in aliases.items():
                        if k + "::" in item:
                            item = item.replace(k + "::", v + "::")
                    if plug not in plugins:
                        plugins[plug] = Plugin(plug, topics_, lookups)
                    plugins[plug].add_async_reader(replace_namespace(item), sched.group(2))
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
                        print(f"Mismatched class count {fl}   line {i + 1}   {line}")
                        raise
            # if we think we are still in a class, but are at the end of the file
            if count != 0:
                raise Exception(f"Error {fl}   {count}   {current}")
    for cls, inh in inh_classes.items():
        for icls in inh:
            idx = lookups.l_index(icls.base)
            if idx >= 0:
                lookups[idx].add_writer(cls)

    return inh_classes, lookups, system_classes, plugins, topics_


def gather(build: str) -> tuple[
    dict[Any, list[Any]], list[str], dict[str, Plugin], Topics, Lookups]:
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
    services = glob.glob('services/**/*.cpp', recursive=True)
    hdrs += get_headers('plugins', True)
    hdrs += get_headers('services', True)
    hdrs += get_headers('src', True)
    hdrs += get_headers('utils', True)
    utils = get_src('utils', True)
    src = get_src('src', True)
    ext = get_src(os.path.join(build, '_deps/audio_pipeline/src/Audio_Pipeline/src'), recursive=True)
    ext += get_src(os.path.join(build, '_deps/OpenVINS/src/OpenVINS'), recursive=True)
    ext += get_src(os.path.join(build, '_deps/hand_tracking/src/HAND_TRACKING'), recursive=True)

    full_temp = hdrs + plugins + services + utils + src + ext
    full = []
    for i in full_temp:
        if not os.path.islink(i):
            full.append(i)
    # extract class information from the files
    inheritances, lookups, system_classes, plugin_list, topics_ = get_classes(full)

    deps = {}
    # loop over inheritance items and organize them
    for cls, data in inheritances.items():
        for inh in data:
            temp = inh.file.split(os.sep)
            file_name = temp[-1]

            # look specifically for plugins
            if temp[0] == 'plugins' or temp[0] == 'services':
                file_name = temp[1]
            elif '_deps' in inh.file:
                file_name = temp[2]
            inh.base = replace_namespace(inh.base)
            if inh.base not in deps:
                deps[inh.base] = []
            deps[inh.base].append({'class': cls, 'from': file_name})
    return deps, system_classes, plugin_list, topics_, lookups


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
    # print(f"{'Base class':25s}  {'Child class':50s}")
    # print(sep)
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
        # print(line)
    return combined


'''
def report_invoke(invoke: Dict[str, Set[str]], readers: Dict[str, Plugin]) -> None:
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
'''

'''
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
'''


def report_plugin_invoke(deps: Dict, system_classes: List, plugins: Dict[str, Plugin], topics_: Topics,
                         lookups: Lookups) -> None:
    """
    Print a table of plugins, what plugin type they depend on, and what plugins provide that type
    to stdout.
    :param deps: Dictionary of parent classes and their children
                   * **key**: parent class name
                   * **value**: children class names
    :param system_classes: listing of system classes
    :param topics_:
    :param plugins:
    :param lookups:
    :type deps: dict[str, list[str]]
    """
    # print("\n\n\n")
    # print(f"{'Plugin':25s}  {'Requires':20s}  {'Provided by':33s}")
    # print(sep)
    plugin_names = list(plugins.keys())
    plugin_names.sort()
    providers = {}

    for t in topics_:
        i_name = f"{t.name} <{t.type}>"
        if i_name not in providers:
            providers[i_name] = []
        providers[i_name] += list(t.writers)
    for lu in lookups:
        i_name = f"<{lu.type}>"
        if i_name not in providers:
            providers[i_name] = []
        providers[i_name] += list(lu.writers)
    yaml_mapper = {"dep_map": []}
    dot_mapper = []

    for plugin_name in plugin_names:
        plugin = plugins[plugin_name]
        uses = []
        for tpc in plugin.readers:
            if tpc.name is None:
                uses.append(f"<{tpc.type}>")
            else:
                uses.append(f"{tpc.name} <{tpc.type}>")
        async_u = [f"{tpc.name} <{tpc.type}>" for tpc in plugin.async_readers]
        uses.sort()
        async_u.sort()
        pl_uses = []
        pl_async_u = []
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
                if plugin_name not in prov_by:
                    pl_uses.append({'needs': pl, "provided_by": prov_by})
            else:
                print(f"Cannot find something that provides {pl}")
        for pl in async_u:
            if pl in deps.keys():
                dd = []
                for x in deps[pl]:
                    if x not in system_classes:
                        dd.append(x.replace('_impl', ''))
                if dd:
                    pl_async_u.append({"needs": pl, "provided_by": dd})
            elif pl in providers.keys():
                prov_by = providers[pl]
                prov_by.sort()
                if plugin_name not in prov_by:
                    pl_async_u.append({'needs': pl, "provided_by": prov_by})
            else:
                print(f"Cannot find something that provides {pl}")
        if pl_uses or pl_async_u:
            pl_deps = []
            pl_async_deps = []
            # print(f"{plugin_name:25s}  {pl_uses[0]['needs']:20s} {', '.join(pl_uses[0]['provided_by'])}")
            for i in range(len(pl_uses)):
                pl_deps.append({'needs': pl_uses[i]['needs'],
                                'provided_by': pl_uses[i]['provided_by']
                                })
            pld_deps = deepcopy(pl_deps)
            for i in range(len(pl_async_u)):
                pl_async_deps.append({'needs': pl_async_u[i]['needs'],
                                      'provided_by': pl_async_u[i]['provided_by']
                                      })
                pl_deps.append({'needs': pl_async_u[i]['needs'],
                                'provided_by': pl_async_u[i]['provided_by']
                                })

            # if i > 0:
            # print(f"{' ':25s}  {pl_uses[i]['needs']:20s} {', '.join(pl_uses[i]['provided_by'])}")
            yaml_mapper["dep_map"].append({'plugin': plugin_name, 'dependencies': deepcopy(pl_deps)})
            dot_mapper.append({'plugin': plugin_name, 'dependencies': deepcopy(pld_deps),
                               "async_dependencies": deepcopy(pl_async_deps)})
    yaml.dump(yaml_mapper, open(os.path.join('plugins', 'plugin_deps.yaml'), 'w'), Dumper=NoAliasDumper)
    # generate dataflow
    with open(os.path.join("docs", "dataflow.dot"), 'w') as fh:
        fh.write("#!/usr/bin/env -S dot -O -Tpng\n")
        fh.write("strict digraph {\n")
        fh.write("// Plugins\n")
        for pl in plugins.values():
            if pl.is_service:
                fh.write(
                    f"  \"pl_{pl.name}\" [label=\"{pl.name}\", shape=\"component\", color=\"blue3\", fillcolor=\"cyan3\", style=\"filled\"];\n")
            else:
                fh.write(
                    f"  \"pl_{pl.name}\" [label=\"{pl.name}\", shape=\"rect\", color=\"blue3\", fillcolor=\"blue3\", style=\"filled\", fontcolor=\"white\"];\n")
        fh.write("\n")

        fh.write("// Lookups\n")
        for lu in lookups:
            if lu.type in system_classes:
                continue
            fh.write(f"  \"t_{lu.type}\" [label=\"<{lu.type}>\", shape=\"doubleoctagon\", color=\"darkgreen\"];\n")
        fh.write("\n")

        odd_writers = []
        odd_readers = []
        odd_async_readers = []
        fh.write("// Topics\n")
        for t in topics_:
            if t.type in lookups_:
                continue
            fh.write(
                f"  \"t_{t.name}_<{t.type}>\" [label=\"{t.name} <{t.type}>\", shape=\"cylinder\", color=\"darkgoldenrod4\"];\n")
            for p in t.writers:
                odd_writers.append([f"{t.name}_<{t.type}>", p])
            for p in t.readers:
                odd_readers.append([f"{t.name}_<{t.type}>", p])
            for p in t.readers:
                odd_async_readers.append([f"{t.name}_<{t.type}>", p])
        fh.write("\n")

        connections = {}
        fh.write("// Readers\n")
        for wr in odd_writers:
            if wr[0] not in connections:
                connections[wr[0]] = set()
            connections[wr[0]].add(wr[1])
        for deps in dot_mapper:
            pn = deps['plugin']
            for d in deps['dependencies']:
                n_name = d['needs'].replace(" ", "_")
                if n_name.startswith('<'):
                    n_name = n_name.replace('<', '').replace('>', '')
                fh.write(f"  \"t_{n_name}\" -> \"pl_{pn}\" [style=\"dashed\"];\n")
                if n_name not in connections:
                    connections[n_name] = set()
                connections[n_name].update(d['provided_by'])
            for d in deps['async_dependencies']:
                n_name = d['needs'].replace(" ", "_")
                fh.write(f"  \"t_{n_name}\" -> \"pl_{pn}\" [style=\"dotted\"];\n")
                if n_name not in connections:
                    connections[n_name] = set()
                connections[n_name].update(d['provided_by'])
        fh.write("\n")
        fh.write("// Writers\n")
        for need, providers in connections.items():
            for pr in providers:
                if pr == "faux_pose":
                    pr = "fauxpose"
                fh.write(f"  \"pl_{pr}\" -> \"t_{need}\" [style=\"solid\"];\n")
        fh.write("}\n")

        fh.close()


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
                temp = line[start + 1:end].split()
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
                temp = line[start + 1:end].split()[-1]
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
                temp = line[start + 1:end]
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
    files += glob.glob(os.path.join(path, 'services', '**', 'CMakeLists.txt'), recursive=True)
    files += glob.glob(os.path.join(path, 'utils', '**', 'CMakeLists.txt'), recursive=True)
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
        if temp[-3] == 'plugins' or temp[-3] == 'services':
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
    dependencies, sys_cls, plugins_, topics, lookups_ = gather(_build)
    comb = report_deps(dependencies)
    # report_invoke(inv, readers_)
    # report_writers(writers_)
    report_plugin_invoke(comb, sys_cls, plugins_, topics, lookups_)
    process_cmake()
