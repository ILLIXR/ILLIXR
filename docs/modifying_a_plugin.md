# Modifying a plugin

## Tutorial

This is how you can modify an existing ILLIXR plugin

1.  Clone the repository for the component you want to modify.
    For example:

    <!--- language: lang-shell -->

        git clone https://github.com/ILLIXR/audio_pipeline.git

1.  Modify the config file like this:

    **Original Config**

    <!--- language: lang-yaml -->

        plugin_group:
          - path: timewarp_gl/
          - name: audio
            path:
              git_repo: https://github.com/ILLIXR/audio_pipeline.git
              version: 3433bb452b2ec661c9d3ef65d9cf3a2805e94cdc

    **New Config**

    <!--- language: lang-yaml -->

        plugin_group:
          - path: timewarp_gl/
          - name: /PATH/TO/LOCAL/AUDIO-PLUGIN
   
1.  See the instructions on [Building ILLIXR][10] to learn how to run ILLIXR.

1.  To push the modification to upstream ILLIXR, push up the changes to the plugin's repository
        and modify the original config with the commit version updated.
    Then create a PR on the main ILLIXR repository.


[//]: # (- Internal -)

[10]:   building_illixr.md
