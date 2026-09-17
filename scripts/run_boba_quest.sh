#!/usr/bin/env bash
set -euo pipefail

# Run the Release desktop build with its plugins on the library search path.
# ILLIXR_BUILD_DIR selects an out-of-tree build; all arguments go to ILLIXR.
illixr_root="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)"
build_dir="${ILLIXR_BUILD_DIR:-${illixr_root}/build}"
if [[ ! -d "${build_dir}" ]]; then
    printf 'ILLIXR build directory does not exist: %s\n' "${build_dir}" >&2
    exit 1
fi
build_dir="$(cd -- "${build_dir}" && pwd)"
if [[ ! -x "${build_dir}/main.opt.exe" ]]; then
    printf 'Build the native Boba Release profile first; executable missing: %s/main.opt.exe\n' "${build_dir}" >&2
    exit 1
fi

# Bash globbing also works in terminals where ripgrep is not installed.
shopt -s globstar nullglob
plugin_libraries=("${build_dir}"/plugins/**/libplugin*.so)
if ((${#plugin_libraries[@]} == 0)); then
    printf 'No built ILLIXR plugins found under %s/plugins\n' "${build_dir}" >&2
    exit 1
fi
plugin_path=""
for library in "${plugin_libraries[@]}"; do
    plugin_path+="${plugin_path:+:}${library%/*}"
done
export LD_LIBRARY_PATH="${plugin_path}${LD_LIBRARY_PATH:+:${LD_LIBRARY_PATH}}"
exec "${build_dir}/main.opt.exe" "$@"
