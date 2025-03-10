---
- author.astro-friedel
---
Environment variable handling has been introduced to the `switchboard`. Calls to `std::getenv` have been replaced with calls to `swithchboard->get_env`, `switchbaord->get_env_char`, or `switchbaord->get_env_bool` and calls to `std::setenv` have been replaced with calls to `switchboard->set_env`. This change allows the switchboard to act as a broker for all environment variables. Additionally, environment variables can now be specified on the command line or in a yaml file, and will be made available to all plugins via the switchboard.
