## modules.json Documentation

The modules.json file contains information necessary to generate lists of 3rd party dependencies for each plugin. This list is used by the [getting_started][1] page to produce yum/dnf/apt command lines for the user to cut and paste to install any necessary dependencies.

There are three primary entries in the file:

  - [**systems**](#systems) - supported operating systems
  - [**plugins**](#plugins) - listing of plugins and their dependencies
  - [**dependencies**](#dependencies) - listing of 3rd party dependencies and OS specific install information

### Systems

The systems section lists each supported operating system and the supported versions in the following format:

``` json
{
  "name": "<OS NAME>",          # the name of the operating system (e.g. Ubuntu, Fedora, etc.)
  "versions": ["<V1>", "<V2>"]  # json array containing the supported version(s) (e.g. 22, 9, 37)
}
```

### Plugins

The plugins section lists each plugin and any 3rd party dependencies (packages) in the following format:

``` json
{
  "name": "<plugin_name">,               # name of the plugin (same as the name of the plugin subdirectory
  "cmake_flag": "USE_<PLUGIN_NAME>=ON",  # name of the plugin in all caps
  "dependencies": ["<d1>", "<d2>"]       # json array listing the dependency names, which must match the name in the dependencies section
}
```

### Dependencies

The dependencies section lists every possible ILLIXR dependency along with how to install each on the different supported operating systems. The entries are in the following format and assumes the supported operating systems as of March 2025:

``` json
{
  "<dependency>": {      # the name of the dependency, must match any uses in the plugins section
    "pkg": {
      "Ubuntu": {
        "pkg": "<package name>",  # the name of the apt package(s) to install to get this dependency, can be a space seperated list, leave as an empty string if this OS does not supply it
        "postnotes": "",          # for any post-installation instructions to properly configure things
        "notes": ""               # for any installation notes or instructions, specifically if the package needs to be manually installed
      },
      "Fedora": {
        "pkg": "<package name>",  # the name of the dnf package(s) to install to get this dependency, can be a space seperated list, leave as an empty string if this OS does not supply it
        "postnotes": "",          # for any post-installation instructions to properly configure things
        "notes": ""               # for any installation notes or instructions, specifically if the package needs to be manually installed
      }
    }
  }
}
```

[1]:  ../getting_started.md
