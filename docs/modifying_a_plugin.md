# Modifying a plugin

## Tutorial

This is how you can modify an existing ILLIXR plugin

1. Clone the repository for the component you want to modify. e.g: `git clone https://github.com/ILLIXR/audio_pipeline.git`

2.  Modify the config file like this:

   - Original Config

     ```
     plugin_group:
       - path: timewarp_gl/
       - name: audio
         path:
           git_repo: https://github.com/ILLIXR/audio_pipeline.git
           version: 3433bb452b2ec661c9d3ef65d9cf3a2805e94cdc
     ```

   - New Config

     ```
     plugin_group:
       - path: timewarp_gl/
       - name: /PATH/TO/LOCAL/PLUGIN
     ```
