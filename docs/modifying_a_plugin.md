# Modifying a plugin

## Tutorial

You can modify an existing ILLIXR [_Plugin_][11] in-place by including a local copy
    of the plugin as a path in the plugin [_Flow_][12].
This tutorial walks through the necessary edits for the `audio` plugin:

1.  Clone the repository for the component you want to modify:

    <!--- language: lang-shell -->

        git clone https://github.com/ILLIXR/audio_pipeline.git /PATH/TO/LOCAL/AUDIO_PLUGIN

    Make sure to specify the target path for your plugin copy.

1.  Pre-defined plugins have [_Configuration_][12] files defined in `ILLIXR/configs/plugins`.
    The `audio.yaml` plugin config file [specifies a path][13] that looks like this:

    <!--- language: lang-yaml -->

        name: audio
        path:
          git_repo: https://github.com/ILLIXR/audio_pipeline.git
          version: "3.0"

    You can modify the `path` object here to point somewhere else or to a different
        version of the plugin.
    Editing `audio.yaml` will modify the plugin for all configurations that include
        the plugin.

    You can also swap the `audio` plugin for your local copy _in-place_.
    The following is a sample plugin group with the pre-defined `audio` config included:

    <!--- language: lang-yaml -->

        plugin_group:
          - !include "plugins/timewarp_gl.yaml"
          - !include "plugins/audio.yaml"

    To swap the plugin, simply change the path to point to the plugin directly:

    <!--- language: lang-yaml -->

        plugin_group:
          - !include "plugins/timewarp_gl.yaml"
          - path: /PATH/TO/LOCAL/AUDIO_PLUGIN

    See the instructions on [Building ILLIXR][10] to learn more about configurations
        and running ILLIXR.

1.  To push the modification to upstream ILLIXR, push up the changes to the plugin's repository
        and modify the original config with the commit version updated.
    Then create a PR on the main ILLIXR repository.


[//]: # (- Internal -)

[10]:   building_illixr.md
[11]:   glossary.md#plugin
[12]:   glossary.md#configuration
[13]:   building_illixr.md#specifying-paths
